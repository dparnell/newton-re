/*
	File:		LowLevel.h

	Contains:	The cursive reader's low level: a word's trace cut into
				the elements (xrs) the reader matches letters against.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	`low_level` takes the trace of one word (the points of its strokes
	one after another, a y of -1 between strokes - `PS_point_type`) and
	the engine's parameters (`rc_type`), and writes an `xrdata_type`: the
	xrs, 0x18-byte elements each naming a feature of the writing (an
	upper or lower extremum, an arc, a crossing, a dot, a break) with
	where it lies and how high.  On the way it works in a `low_type`,
	a block of state on its stack:

	  - the trace, as parallel arrays of x and y (`fX`/`fY`, `fII`
	    points; a y of -1 is a pen-up, one at each end and one between
	    strokes), and the same arrays as they were first filled
	    (`fXInitial`/`fYInitial`);
	  - four working buffers of `kLowBufferSize` shorts (`fBuffers`),
	    which the filters write into and swap with the trace: 0 and 1
	    are x and y, 2 and 3 map a point back to the point it came from;
	  - the strokes' extents and boxes (`fGroups`, a `POINTS_GROUP` for
	    each stroke);
	  - the special elements found so far (`fSpecl`, a list of
	    `SPEC_TYPE`s: an extremum, an arc, a crossing ...), which the
	    later passes turn into xrs.

	All of it is in one allocation (`LowAlloc`) but for the special
	elements (`AllocSpecl`) and the stroke-description array
	(`CreateSDS`).

	DEVIATION: the ROM's `low_type` is 0x9c bytes and its `SPEC_TYPE`
	0x14; both hold pointers, so on the host they are bigger - every
	allocation and clearing of them is `sizeof` of the host's type, and
	a walk along an array of them is by element rather than by 0x14
	bytes.  The fields are at the ROM's offsets in the comments.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __LOWLEVEL_H
#define __LOWLEVEL_H

#ifndef __WORDSEGMENT_H
#include "WordSegment.h"
#endif

struct rc_type;
struct xrdata_type;

// A box: left, top, right, bottom (ROM _RECT, 8 bytes).
struct _RECT
{
	short			left;
	short			top;
	short			right;
	short			bottom;
};

// A stroke's extent in the trace and its box (12 bytes).
struct POINTS_GROUP
{
	short			iBeg;			// +00  the stroke's first point
	short			iEnd;			// +02  and its last
	_RECT			box;			// +04
};

// A special element (ROM 0x14 bytes): what it is, the part of the
// trace it covers, and its neighbours in the list.
struct SPEC_TYPE
{
	UByte			mark;			// +00  what it is (0 none; the kinds are the ROM's own numbers)
	UByte			code;			// +01
	UByte			attr;			// +02
	UByte			other;			// +03
	short			iBeg;			// +04  the first point it covers
	short			iEnd;			// +06  and the last
	short			ipoint0;		// +08  two more points (-2 for none)
	short			ipoint1;		// +0a
	SPEC_TYPE*		next;			// +0c
	SPEC_TYPE*		prev;			// +10
};

// An extremum as the base-line finder keeps it (ROM 0x10 bytes): its
// suspicion code, where it is, the original point it came from, and the
// element it was copied from.
struct EXTR
{
	short			susp;			// +00  0 on the line; the codes are in LowBaseline.cpp
	short			x;				// +02
	short			y;				// +04
	short			i;				// +06  the point in the original trace (buffer 2's map)
	short			shift;			// +08  its stroke's shift to the left: the gaps before it closed up (extract_all_extr; the stroke end's attr is its low byte)
	short			fa;				// +0a
	SPEC_TYPE*		elem;			// +0c
};

// One of the four working buffers: where it is and how many shorts
// (ROM 8 bytes).
struct low_buffer
{
	short*			ptr;			// +00
	short			size;			// +04
};

// A stroke description (ROM 0x2c bytes, no pointers, so the same on
// the host): a piece of the trace between two points as
// iMostFarDoubleSide describes it (LowPict.cpp).  A stroke's head (attr
// 0x10) uses slope, crook, length, lengthRatio and share for other
// things (StrElements).
struct _SDS_TYPE
{
	UByte			mark;			// +00
	UByte			attr;			// +01  0x80 the chord crosses the piece, 0x81 not; 0x10 a head, 0x20 a tail
	short			iBeg;			// +02
	short			iEnd;			// +04
	short			xMax;			// +06
	short			xMin;			// +08
	short			yMax;			// +0a
	short			yMin;			// +0c
	short			f0e;			// +0e
	short			chord;			// +10  the chord's length
	short			slope;			// +12  hundredths of dy/dx (0x7fff upright; a head: its own index)
	short			distA;			// +14  the furthest point on one side of the chord, how far
	short			iA;				// +16  and which
	short			distB;			// +18  on the other side
	short			iB;				// +1a
	short			maxDist;		// +1c  the further of the two
	short			iMax;			// +1e
	int32_t			length;			// +20  along the trace
	short			crook;			// +24  maxDist in hundredths of the chord (a head: the longest piece's index + 1)
	short			lengthRatio;	// +26  length in hundredths of the chord (a head: the stroke's code)
	short			share;			// +28  the piece's share of the stroke's length, in hundredths (a head: the stroke's attr)
	short			f2a;			// +2a
};

// The stroke descriptions' array and how full it is (ROM 12 bytes).
struct _SDS_CONTROL_TYPE
{
	short			sizeSDS;		// +00  room for how many
	short			f02;			// +02  where the stroke being described starts
	short			lenSDS;			// +04  how many (-2 once destroyed)
	_SDS_TYPE*		pSDS;			// +08
};

enum
{
	kLowBufferSize = 0xce7,			// the shorts in each working buffer, and the most points a trace may have
	kLowSpeclSize = 400,			// the special elements' room
	kLowIndexSize = 0x32,			// fIndex's room
	kLowMaxGroups = 0x32,			// the most strokes
	kSDSElementSize = 0x2c			// ROM size of a _SDS_TYPE
};

struct low_type
{
	rc_type*		rc;				// +00
	low_buffer		fBuffers[4];	// +04  (+04/+08, +0c/+10, +14/+18, +1c/+20)
	PS_point_type*	fTrace;			// +24  the trace as it was handed in
	short			fMaxPoints;		// +28  the room in fX/fY and in the initial arrays
	short*			fXInitial;		// +2c
	short*			fYInitial;		// +30
	short*			fX;				// +34
	short*			fY;				// +38
	short			fII;			// +3c  how many points
	SPEC_TYPE*		fSpecl;			// +40
	short			fNMaxSpecl;		// +44
	short			fLenSpecl;		// +46  how many are used
	short			fLastSpecl;		// +48  the last one's index
	short*			fIndex;			// +4c
	short			fLenIndex;		// +50
	short			fSizeIndex;		// +52
	POINTS_GROUP*	fGroups;		// +54  one per stroke
	short			fLenGroups;		// +58
	short			fMaxGroups;		// +5a
	short			f5c;			// +5c  0x7fff
	_SDS_CONTROL_TYPE*	fSDS;		// +60
	short			fLenBars;		// +64  how many of fBars are used
	short			f66;
	POINTS_GROUP*	fBars;			// +68  the upright sticks (VertSticksSelector) Pict judges bars against, room for 80
	UByte			f6c[2];			// +6c
	short			fSlope;			// +6e  the writing's slant (from rc +0xac, back to it at the end)
	short			fStep;			// +70  how wide the writing steps across (lk_begin: DefineWritingStep)
	short			fStepKind;		// +72  how it was measured (DefineWritingStep's answer)
	_RECT			fBox;			// +74  the trace's box
	short			fThresh[16];	// +7c..+9b  the heights the later passes compare with (DefLineThresholds, which lists them)
};

// The engine's arithmetic.  A product or sum that can overflow a word is
// made with these, which wrap as the ARM does rather than trap.
inline long	LMul(long a, long b)	{ return (int32_t) ((uint32_t) a * (uint32_t) b); }
inline long	LAdd(long a, long b)	{ return (int32_t) ((uint32_t) a + (uint32_t) b); }
long	HWRLAbs(long x);											// ROM 0x000e6508 HWRLAbs__Fl
long	HWRMathISqrt(short x);										// ROM 0x002e61c0 HWRMathISqrt__Fs - the root rounded to the nearest, over SQRTa/SQRTb; 0 for a negative x
long	HWRMathILSqrt(long x);										// ROM 0x002e615c HWRMathILSqrt__Fl - brought under 0x8000 by quarters and the root doubled back; 0x7fff when it overflows a short

// The low level's life.
long	low_level(PS_point_type* trace, xrdata_type* xr, rc_type* rc);		// ROM 0x0034ea74 low_level__FP13PS_point_typeP11xrdata_typeP7rc_type - ==> 0, 1 for a failure
long	PrepareLowData(low_type* low, PS_point_type* trace, rc_type* rc, short** block);	// ROM 0x0034e99c PrepareLowData__FP8low_typeP13PS_point_typeP7rc_typePPs - ==> 1, 0 for a failure (everything let go)
void	FillLowDataTrace(low_type* low, PS_point_type* trace);		// ROM 0x0034e8f8 FillLowDataTrace__FP8low_typeP13PS_point_type
void	GetLowDataRect(low_type* low);								// ROM 0x0034e968 GetLowDataRect__FP8low_type
long	LowAlloc(short** block, short nBuffers, short bufferSize, low_type* low);	// ROM 0x00305a28 LowAlloc__FPPssT2P8low_type - ==> 0, 1 for no room
void	low_dealloc(short** block);									// ROM 0x00306f74 low_dealloc__FPPs
void	SetXYToInitial(low_type* low);								// ROM 0x00305a14 SetXYToInitial__FP8low_type
long	AnalyzeLowData(low_type* low, PS_point_type* trace);		// ROM 0x0034ed54 AnalyzeLowData__FP8low_typeP13PS_point_type - the special elements found and coded; ==> 0, 1 for a failure
long	BaselineAndScale(low_type* low);							// ROM 0x0034eba0 BaselineAndScale__FP8low_type - the trace filtered, its extrema found and the base line found; ==> 0, 1 for a failure
long	transfrmN(low_type* low);									// ROM 0x001baaf8 transfrmN__FP8low_type - the base-line finder: the borders found, the trace rescaled to them; ==> 0, 1 for a failure
extern const short	const1[26];										// the engine's constants: [0] the filter's scale (10), [5] the default extremum step (8), [13] how far apart two points of one stroke must be to cross (8)
short	MaxPointsGrown(short n);									// ROM 0x00307f50 MaxPointsGrown__Fs
Boolean	AllocSpecl(SPEC_TYPE** specl, short n);						// ROM 0x00306410 AllocSpecl__FPP9SPEC_TYPEs
void	DeallocSpecl(SPEC_TYPE** specl);							// ROM 0x00307984 DeallocSpecl__FPP9SPEC_TYPE
long	InitSpecl(low_type* low, short n);							// ROM 0x002ba430 InitSpecl__FP8low_types - the list emptied, element 0 its head; ==> 0
Boolean	InitSpeclElement(SPEC_TYPE* elem);							// ROM 0x002ba4cc InitSpeclElement__FP9SPEC_TYPE - ==> whether elem was nil
Boolean	CreateSDS(low_type* low, short n);							// ROM 0x0032f8bc CreateSDS__FP8low_types
void	DestroySDS(low_type* low);									// ROM 0x0032f91c DestroySDS__FP8low_type

SPEC_TYPE*	NewSPECLElem(low_type* low);							// ROM 0x0030a668 NewSPECLElem__FP8low_type - ==> nil when full
void	DelFromSPECLList(SPEC_TYPE* elem);							// ROM 0x0030a6d0 DelFromSPECLList__FP9SPEC_TYPE
SPEC_TYPE*	FindMarkRight(SPEC_TYPE* elem, UByte mark);				// ROM 0x0030a6f4 FindMarkRight__FP9SPEC_TYPEUc
SPEC_TYPE*	FindMarkLeft(SPEC_TYPE* elem, UByte mark);				// ROM 0x0030a714 FindMarkLeft__FP9SPEC_TYPEUc
void	DelThisAndNextFromSPECLList(SPEC_TYPE* elem);				// ROM 0x0030a734 DelThisAndNextFromSPECLList__FP9SPEC_TYPE
void	DelCrossingFromSPECLList(SPEC_TYPE* elem);					// ROM 0x0030a760 DelCrossingFromSPECLList__FP9SPEC_TYPE
void	SwapThisAndNext(SPEC_TYPE* elem);							// ROM 0x0030a764 SwapThisAndNext__FP9SPEC_TYPE
void	Insert2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second);		// ROM 0x0030a798 Insert2ndAfter1st__FP9SPEC_TYPET1
void	InsertCrossing2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second);	// ROM 0x0030a7b8 InsertCrossing2ndAfter1st__FP9SPEC_TYPET1
void	Move2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second);		// ROM 0x0030a7e4 Move2ndAfter1st__FP9SPEC_TYPET1
void	MoveCrossing2ndAfter1st(SPEC_TYPE* first, SPEC_TYPE* second);	// ROM 0x0030a810 MoveCrossing2ndAfter1st__FP9SPEC_TYPET1
void	RefreshElem(SPEC_TYPE* elem, UByte mark, UByte code, UByte attr);	// ROM 0x0030a83c RefreshElem__FP9SPEC_TYPEUcN22
Boolean	IsUpperElem(SPEC_TYPE* elem);								// ROM 0x00305b84 IsUpperElem__FP9SPEC_TYPE
Boolean	IsLowerElem(SPEC_TYPE* elem);								// ROM 0x00305bc0 IsLowerElem__FP9SPEC_TYPE

// The strokes.
long	InitGroupsBorder(low_type* low, short withBoxes);			// ROM 0x003087e0 InitGroupsBorder__FP8low_types - ==> 0, 1 for a trace that does not start and end with a pen-up or has too many strokes
Boolean	ClearGroupsBorder(low_type* low);							// ROM 0x0030897c ClearGroupsBorder__FP8low_type
long	GetGroupNumber(low_type* low, long i);						// ROM 0x003089cc GetGroupNumber__FP8low_typei - the stroke point i is in; -2 for a pen-up or none
long	IsPointCont(low_type* low, long i, UByte mark);				// ROM 0x00308a58 IsPointCont__FP8low_typeiUc - 5 inside an element of that mark, 3 at its start, 4 at its end, -2 none

// The trace.
void	trace_to_xy(short* x, short* y, long n, PS_point_type* trace);	// ROM 0x00306074 trace_to_xy__FPsT1iP13PS_point_type
long	GetBoxFromTrace(PS_point_type* trace, long iBeg, long iEnd, _RECT* box);	// ROM 0x00307510 GetBoxFromTrace__FP13PS_point_typeiT2P5_RECT - ==> 1
void	GetTraceBox(short* x, short* y, long iBeg, long iEnd, _RECT* box);	// ROM 0x003075b4 GetTraceBox__FPsT1iT3P5_RECT
long	xMinMax(long iBeg, long iEnd, short* x, short* y, short* xMin, short* xMax);	// ROM 0x00307358 xMinMax__FiT1PsN33 - over the points not pen-ups; ==> 1
long	yMinMax(long iBeg, long iEnd, short* y, short* yMin, short* yMax);	// ROM 0x003073cc yMinMax__FiT1PsN23 - ==> 1
long	iMidPointPlato(long i, long iEnd, short* a, short* y);	// ROM 0x00306f24 iMidPointPlato__FiT1PsT3 - the middle of the run of equal values from i
long	ixMin(long iBeg, long iEnd, short* x, short* y);			// ROM 0x00306f9c ixMin__FiT1PsT3 - ==> -1 for none
long	ixMax(long iBeg, long iEnd, short* x, short* y);			// ROM 0x0030700c ixMax__FiT1PsT3
long	iMostFarFromChord(short* x, short* y, long i, long j);		// ROM 0x00306448 iMostFarFromChord__FPsT1iT3 - the point from i to j furthest from their chord (the middle of a run of equals)
short	NewIndex(short* index, short* y, short i, short n, short mode);	// ROM 0x00307cd0 NewIndex__FPsT1sN23 - where an old point index went after filtering: 0 the first new point from it, 2 the last, 1 between the two; -2 none

// The special elements found.
long	Extr(low_type* low, short step, short eps1, short eps2, short eps3, short depth, short flags);	// ROM 0x002ba52c Extr__FP8low_typesN52 - every stroke's extrema in the directions flags asks for; ==> 0, 1 for no room
long	MarkSpecl(low_type* low, SPEC_TYPE* elem);					// ROM 0x002bc36c MarkSpecl__FP8low_typeP9SPEC_TYPE - a copy added to the list; ==> 0, 1 for no room
long	Mark(low_type* low, UByte mark, UByte code, UByte attr, UByte other, short iBeg, short iEnd, short ipoint0, short ipoint1);	// (LowExtr.cpp) - ==> 0, 1 for no room
long	NoteSpecl(low_type* low, SPEC_TYPE* src, SPEC_TYPE* specl, short* len, short max);	// (LowExtr.cpp) - ==> 1, 0 when full

// The base-line finder's pieces (LowBaseline.cpp).
void	sort_extr(EXTR* extr, long n);								// ROM 0x001bd73c sort_extr__FP4EXTRi - into order of x
long	calc_average(short* a, long n);								// ROM 0x001c0ea4 calc_average__FPsi - ==> 1 for none
long	calc_mediana(short* a, long n);								// ROM 0x001c5068 calc_mediana__FPsi - ==> 1 for none
long	extract_ampl(low_type* low, short* ampl, long* count);		// ROM 0x001c4f74 extract_ampl__FP8low_typePsPi - the letters' heights; ==> 0, 1 for more than 100
long	sign(long a, long b);										// ROM 0x001c17e4 sign__FiT1
Boolean	pnt(_RECT box, long k);										// ROM 0x001bcc2c pnt__F5_RECTi - under a third of k+1 both ways
long	straight_stroke(long i, long j, short* x, short* y, long k);	// ROM 0x001bd198 straight_stroke__FiT1PsT3T1
long	str_com(long i, long j, short* x, short* y, long k);		// ROM 0x001bcb90 str_com__FiT1PsT3T1
void	ret_to_line(EXTR* extr, long n, long i, long j);			// ROM 0x001c0c34 ret_to_line__FP4EXTRiN22
void	spec_neibour_extr(EXTR* extr, long n, UByte kind, long dir);	// ROM 0x001bf7ec spec_neibour_extr__FP4EXTRiUcT2
void	super_min_to_line(EXTR* extr, long n, short* base, long a, long b, long* count);	// ROM 0x001bf75c super_min_to_line__FP4EXTRiPsN22Pi
long	delete_line_extr(EXTR* extr, long* n, long code);			// ROM 0x001c0d14 delete_line_extr__FP4EXTRPii - ==> 1
long	insert_line_extr(low_type* low, SPEC_TYPE* elem, EXTR* extr, long* n);	// ROM 0x001c0d88 insert_line_extr__FP8low_typeP9SPEC_TYPEP4EXTRPi - ==> 1
long	sub_max_to_line(low_type* low, EXTR* extr, long* n, short* base, long lim);	// ROM 0x001bf6a4 sub_max_to_line__FP8low_typeP4EXTRPiPsi
long	calc_ampl(EXTR e, short* y, UByte kind);					// ROM 0x001c0ab0 calc_ampl__F4EXTRPsUc
long	is_defis(low_type* low, long nStrokes);						// ROM 0x001c4c4c is_defis__FP8low_typei
long	del_tail_min(EXTR* extr, long* n, short* y, short* base, UByte flag);	// ROM 0x001c239c del_tail_min__FP4EXTRPiPsT3Uc - ==> 1
long	point_of_smooth_bord(long i, long n, EXTR* extr, low_type* low, long w);	// ROM 0x001c0ee0 point_of_smooth_bord__FiT1P4EXTRP8low_typeT1 - the line's height at point i from the extrema within w of it
void	smooth_d_bord(EXTR* extr, long n, low_type* low, long w, short* line);	// ROM 0x001c11ec smooth_d_bord__FP4EXTRiP8low_typeT2Ps
void	smooth_u_bord(EXTR* extr, long n, low_type* low, long w, short* line, short* base);	// ROM 0x001c1308 smooth_u_bord__FP4EXTRiP8low_typeT2PsT5
long	neibour_susp_extr(EXTR* extr, long n, UByte kind, short* base, long lim);	// ROM 0x001bf8ac neibour_susp_extr__FP4EXTRiUcPsT2 - ==> 0, 1 for fewer than two unsuspected
long	fill_i_point(short* order, low_type* low);					// ROM 0x001c17fc fill_i_point__FPsP8low_type - the points in order of x; ==> how many
long	correct_narrow_ends(EXTR* extr, long* n, EXTR* src, long m, long dy, UByte which);	// ROM 0x001c3b94 correct_narrow_ends__FP4EXTRPiT1iT4Uc - ==> 1
long	non_super(EXTR* extr, long k, short* x, short* y, short* upper);	// ROM 0x001bf578 non_super__FP4EXTRiPsN23
long	non_sub(SPEC_TYPE* elem, short* x, short* y, long eps);		// ROM 0x001beb90 non_sub__FP9SPEC_TYPEPsT2i
long	extract_num_extr(low_type* low, UByte kind, EXTR* extr, long* count);	// ROM 0x001bdff0 extract_num_extr__FP8low_typeUcP4EXTRPi - ==> 0, 1 for more than 50

// The plane geometry (LowGeometry.cpp).
long	QDistFromChord(long ax, long ay, long bx, long by, long px, long py);	// ROM 0x00305cec QDistFromChord__FiN51 - the square of P's distance from line AB
long	is_cross(short xa, short ya, short xb, short yb, short xc, short yc, short xd, short yd);	// ROM 0x003060c8 is_cross__FsN71 - whether segments AB and CD cross
long	FindCrossPoint(short xa, short ya, short xb, short yb, short xc, short yc, short xd, short yd, short* px, short* py);	// ROM 0x003061dc FindCrossPoint__FsN71PsT9 - where lines AB and CD meet; ==> whether on both segments
long	cos_pointvect(long xa, long ya, long xb, long yb, long xc, long yc, long xd, long yd);	// ROM 0x00307ad8 cos_pointvect__FiN71 - the cosine between AB and CD in hundredths
long	cos_horizline(long i, long j, short* x, short* y);		// ROM 0x00307c18 cos_horizline__FiT1PsT3 - the cosine of the trace from i to j with the horizontal
long	xHardOverlapRect(_RECT* a, _RECT* b, ULong strict);			// ROM 0x00307eb0 xHardOverlapRect__FP5_RECTT1Ui
long	yHardOverlapRect(_RECT* a, _RECT* b, ULong strict);			// ROM 0x00307f60 yHardOverlapRect__FP5_RECTT1Ui
long	HardOverlapRect(_RECT* a, _RECT* b, ULong strict);			// ROM 0x00308008 HardOverlapRect__FP5_RECTT1Ui

// The stroke classifier's tests (LowPunct.cpp).
long	extract_all_extr(low_type* low, UByte kind, EXTR* extr, long* all, long* count, short* shift);	// ROM 0x001bc434 extract_all_extr__FP8low_typeUcP4EXTRPiT4Ps - ==> 0, 1 for more than 50
long	com(low_type* low, SPEC_TYPE* elem, long i, long j, long k);	// ROM 0x001c559c com__FP8low_typeP9SPEC_TYPEiN23 - a comma: a straight one or a curved one
long	curve_com_or_brkt(low_type* low, SPEC_TYPE* elem, long i, long j, long k, UShort kind);	// ROM 0x001bc6c8 curve_com_or_brkt__FP8low_typeP9SPEC_TYPEiN23Us - a curved comma (0x10) or bracket (0x20): its bulge's sign, x10 for a sure one; 0 none
long	lead_punct(low_type* low);									// ROM 0x001bcc78 lead_punct__FP8low_type - leading punctuation: 0 none, 1 a comma-like first stroke, 2 two of them
long	end_punct(low_type* low, SPEC_TYPE* end, long k);			// ROM 0x001c519c end_punct__FP8low_typeP9SPEC_TYPEi - a stroke as trailing punctuation: 0 no, 1 a mark of its own, 2 part of a colon or the like
long	hor_stroke(SPEC_TYPE* end, short* x, short* y, long nStrokes);	// ROM 0x001bcdec hor_stroke__FP9SPEC_TYPEPsT2i - whether a stroke is (or has) a horizontal bar
long	is_i_point(low_type* low, SPEC_TYPE* end, _RECT box, long k);	// ROM 0x001bd24c is_i_point__FP8low_typeP9SPEC_TYPE5_RECTi - a dot over an earlier top, which is marked attr 5
long	is_umlyut(SPEC_TYPE* end, _RECT box, long i, long j, short* x, short* y, long k);	// ROM 0x001bd42c is_umlyut__FP9SPEC_TYPE5_RECTiT3PsT5T3 - an umlaut's dots over an earlier letter
long	is_t_min(SPEC_TYPE* elem, short* x, short* y, _RECT box, long k, long i, long j, UByte flag, long* height);	// ROM 0x001c15ac is_t_min__FP9SPEC_TYPEPsT25_RECTiN25UcPi - a t's stem crossed by the bar from i to j
long	extrs_open(low_type* low, SPEC_TYPE* elem, UByte kind, long n);	// ROM 0x001c347c extrs_open__FP8low_typeP9SPEC_TYPEUci - whether nothing of the stroke closes over the extremum

// The line's gaps and glitches (LowLine.cpp).
extern const int	TG1[12];			// the gaps' slopes, [start/middle/end][tops/bottoms][up/down]
extern const int	TG2[12];			// the glitches' slopes
extern const int	H1[12];				// the gaps' heights, in percent
extern const int	H2[12];				// the glitches' heights
extern const int	CS[1];				// the cosine (hundredths) a bend is not a gap past
void	find_gaps_in_line(EXTR* extr, long n, long mode, long lim, UByte kind, long xStart, long xEnd, short* line, short* y, ULong fast, ULong wide);	// ROM 0x001bd7d0 find_gaps_in_line__FP4EXTRiN22UcN22PsT8UiUi
void	find_glitches_in_line(EXTR* extr, long n, long lim, UByte kind, long xStart, long xEnd, short* line, short* x, short* y, long maxWidth, ULong fast, ULong wide);	// ROM 0x001be17c find_glitches_in_line__FP4EXTRiT2UcN22PsN27T2UiUi
void	glitch_to_sub_max(low_type* low, EXTR* extr, long n, long lim, ULong sure);	// ROM 0x001be7e0 glitch_to_sub_max__FP8low_typeP4EXTRiT3Ui
void	glitch_to_inside(EXTR* extr, long n, UByte kind, short* y, long k, long xStart, long xEnd);	// ROM 0x001bed40 glitch_to_inside__FP4EXTRiUcPsN32
void	glitch_to_super_min(EXTR* extr, long n, short* line, long lim, short* x, short* y, ULong sure);	// ROM 0x001bf1c8 glitch_to_super_min__FP4EXTRiPsT2N23Ui
void	all_susp_extr(EXTR* extr, long n, long unused, UByte kind, short* y, long mid, long unused2, long small, short* line, long big);	// ROM 0x001c0844 all_susp_extr__FP4EXTRiT2UcPsN32T5T2

// The borders (LowBorders.cpp).
void	SpecBord(low_type* low, short* dLine, short* uLine, long* lowerY, long* upperY, long* height, long* count, ULong wide, EXTR* extr, long n);	// ROM 0x001c4ce0 SpecBord__FP8low_typePsT2PiN34UiP4EXTRi - level borders from what the caller is sure of
long	calc_med_heights(low_type* low, EXTR* a, EXTR* b, short* upper, short* lower, short* order, long na, long nb, long total, long* height, long* upperMed, long* lowerMed);	// ROM 0x001c3660 calc_med_heights__FP8low_typeP4EXTRT2PsN24iN27PiPiPi - ==> 0, 1 for no memory
long	FillRCNB(short* order, long n, low_type* low, short* upper, short* lower);	// ROM 0x001c4a4c FillRCNB__FPsiP8low_typeN21 - the borders at ten places into rc +0x98; ==> 0, 1 for no points
long	line_pos_mist(low_type* low, long upperY, long lowerY, long height, long nUp, long nDown, EXTR* bottoms, long* upShift, long* downShift, short* upper, short* lower, UByte wide);	// ROM 0x001c24bc line_pos_mist__FP8low_typeiN42P4EXTRPiT8PsPsUc - ==> how badly the borders fit
long	num_bord_correction(EXTR* extr, long* n, long mode, UByte kind, long lim, short* line, short* y);	// ROM 0x001c1960 num_bord_correction__FP4EXTRPiiUcT3PsT6
long	bord_correction(low_type* low, EXTR* extr, long* nPtr, long mode, UByte kind, long mid, long lim, long subLim, long small, long xStart, long xEnd, long neighbour, UByte useNeighbour, short* line, long superLim, long big, ULong superSure, ULong subSure);	// ROM 0x001c1d04 bord_correction__FP8low_typeP4EXTRPiiUcN74T5PsN24UiUi

// The stroke classifiers (LowClassify.cpp).
long	classify_strokes(low_type* low, long mid, long most, long n, long* stem, long* tall, ULong* simple);	// ROM 0x001bfb18 classify_strokes__FP8low_typeiN22PiT5PUi - ==> the number of strokes
long	classify_num_strokes(low_type* low, long* height);			// ROM 0x001c3d40 classify_num_strokes__FP8low_typePi - ==> the number of strokes
long	numbers_in_text(low_type* low, short* upper, short* lower);	// ROM 0x001c4368 numbers_in_text__FP8low_typePsT2 - ==> 1 the word is figures

// AnalyzeLowData's passes (LowAnalyze.cpp).
void	DefLineThresholds(low_type* low);							// ROM 0x002f8b0c DefLineThresholds__FP8low_type
void	OperateSpeclArray(low_type* low);							// ROM 0x0032f5f0 OperateSpeclArray__FP8low_type - the empty strokes taken out of the array
long	Sort_specl(SPEC_TYPE* head, short n);						// ROM 0x002f9ec0 Sort_specl__FP9SPEC_TYPEs - ==> 0, 1 for a failure
long	Clear_specl(SPEC_TYPE* head, short n);						// ROM 0x002fa20c Clear_specl__FP9SPEC_TYPEs - ==> 0, 1 for a list that is not strokes
long	Surgeon(low_type* low);										// ROM 0x0032f6dc Surgeon__FP8low_type - ==> 0
long	measure_slope(low_type* low);								// ROM 0x00320bd0 measure_slope__FP8low_type - the slant in hundredths
long	look_like_circle(SPEC_TYPE* elem, SPEC_TYPE* prev, SPEC_TYPE* next, short* y);	// ROM 0x002bc6a0 look_like_circle__FP9SPEC_TYPEN21Ps

// Pict and the stroke descriptions (LowPict.cpp).
extern const short	maxA_H_end[100];		// the trained limits for a stroke from band [row] down to band [col]
extern const short	maxCR_H_end[100];
extern const short	minL_H_end[100];
extern const short	maxX_H_end[100];
extern const short	maxY_H_end[100];
long	DistanceSquare(long i, long j, short* x, short* y);			// ROM 0x00309818 DistanceSquare__FiT1PsT3
long	Distance8(long x1, long y1, long x2, long y2);				// ROM 0x00305f90 Distance8__FiN31
long	CurvMeasure(short* x, short* y, long i, long j, long k);	// ROM 0x00305df4 CurvMeasure__FPsT1iN23 - the bend in hundredths of the chord, signed
long	HeightInLine(short y, low_type* low);						// ROM 0x00306e2c HeightInLine__FsP8low_type - the band 1..13 between the thresholds
long	iyMin(long iBeg, long iEnd, short* y);						// ROM 0x0030707c iyMin__FiT1Ps
long	iyMax(long iBeg, long iEnd, short* y);						// ROM 0x003070e8 iyMax__FiT1Ps
long	iYup_range(short* y, long iBeg, long iEnd);					// ROM 0x00307438 iYup_range__FPsiT2
long	iYdown_range(short* y, long iBeg, long iEnd);				// ROM 0x003074a8 iYdown_range__FPsiT2
long	iClosestToXY(long iBeg, long iEnd, short* x, short* y, short px, short py);	// ROM 0x00307614 iClosestToXY__FiT1PsT3sT5
long	R_ClosestToLine(short* x, short* y, PS_point_type* p, POINTS_GROUP* group, short* at);	// ROM 0x00307db0 R_ClosestToLine__FPsT1P13PS_point_typeP12POINTS_GROUPT1
void	InitElementSDS(_SDS_TYPE* sds);								// ROM 0x0032f5e4 InitElementSDS__FP9_SDS_TYPE
Boolean	Init_SDS_Element(_SDS_TYPE* sds);							// ROM 0x0032eb24 Init_SDS_Element__FP9_SDS_TYPE
long	NoteSDS(_SDS_CONTROL_TYPE* control, _SDS_TYPE* sds);		// ROM 0x0032f84c NoteSDS__FP17_SDS_CONTROL_TYPEP9_SDS_TYPE - ==> 1, 0 when full
long	HordIntersectDetect(_SDS_TYPE* sds, short* x, short* y);	// ROM 0x0032f19c HordIntersectDetect__FP9_SDS_TYPEPsT2
long	iMostFarDoubleSide(short* x, short* y, _SDS_TYPE* sds, short* px, short* py, ULong withLength);	// ROM 0x0032eb80 iMostFarDoubleSide__FPsT1P9_SDS_TYPEN21Ui
long	CrookCalc(low_type* low, short* maxDist, long i, long j);	// ROM 0x00026338 CrookCalc__FP8low_typePsiT3
long	BildHigh(short top, short bottom, short* h);				// ROM 0x0032e65c BildHigh__FsT1Ps - the eleven heights
long	RelHigh(short* y, long i, long j, short* h, short* bottomBand, short* topBand);	// ROM 0x0032e39c RelHigh__FPsiT2N31
long	RareAngle(low_type* low, SPEC_TYPE* elem, SPEC_TYPE* list, short* count);	// ROM 0x0032f26c RareAngle__FP8low_typeP9SPEC_TYPET2Ps
long	StrElements(low_type* low, SPEC_TYPE* elem, short* heights);	// ROM 0x0032d510 StrElements__FP8low_typeP9SPEC_TYPEPs - ==> 0, 1 for no room
Boolean	DownStepOK(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b);		// ROM 0x000263d0 DownStepOK__FP8low_typeP9SPEC_TYPET2
Boolean	ArcTurnsOK(low_type* low, long kind, long i, long j);		// ROM 0x0002620c ArcTurnsOK__FP8low_type9_ARC_TYPEiT3
long	SlashArcs(low_type* low, long iBeg, long iEnd);				// ROM 0x00025ef0 SlashArcs__FP8low_typeiT2 - ==> 0, 1 for no room
long	FieldSt(_SDS_TYPE* sds, short bottom, short top, short k, short* maxA, short* maxCR, short* minL);	// ROM 0x0032dd54 FieldSt__FP9_SDS_TYPEsN22PsN25
long	Dot(low_type* low, SPEC_TYPE* elem, _SDS_TYPE* head);		// ROM 0x0032dfd4 Dot__FP8low_typeP9SPEC_TYPEP9_SDS_TYPE - ==> 8 a dot, 0 not
long	Close_To(low_type* low, POINTS_GROUP* a, POINTS_GROUP* b);	// ROM 0x0032cdb4 Close_To__FP8low_typeP12POINTS_GROUPT2 - a narrowed to where it meets b
long	Box_Cover(low_type* low, POINTS_GROUP* a, POINTS_GROUP* b);	// ROM 0x0032cc9c Box_Cover__FP8low_typeP12POINTS_GROUPT2
long	Find_Cross(low_type* low, PS_point_type* p, POINTS_GROUP* a, POINTS_GROUP* b);	// ROM 0x0032cad4 Find_Cross__FP8low_typeP13PS_point_typeP12POINTS_GROUPT3
long	IsAnythingShift(low_type* low, POINTS_GROUP* a, POINTS_GROUP* b, short aSide, short bSide);	// ROM 0x0032d08c IsAnythingShift__FP8low_typeP12POINTS_GROUPT2sT4
Boolean	BoxSmallOK(short i, short j, short* x, short* y);			// ROM 0x0032dcdc BoxSmallOK__FsT1PsT3
long	VertStickBorders(low_type* low, SPEC_TYPE* elem, POINTS_GROUP* group);	// ROM 0x0032d1c0 VertStickBorders__FP8low_typeP9SPEC_TYPEP12POINTS_GROUP
void	VertSticksSelector(low_type* low);							// ROM 0x0032da64 VertSticksSelector__FP8low_type
long	YFilter(low_type* low, _SDS_TYPE* piece, SPEC_TYPE* elem);	// ROM 0x0032bacc YFilter__FP8low_typeP9_SDS_TYPEP9SPEC_TYPE
long	SPDClass(low_type* low, short kind, SPEC_TYPE* elem, _SDS_TYPE* head);	// ROM 0x0032f960 SPDClass__FP8low_typesP9SPEC_TYPEP9_SDS_TYPE - ==> 7 a stick, 0 not
long	InStr(low_type* low, _SDS_TYPE* head, SPEC_TYPE* elem, short* heights);	// ROM 0x0032fd24 InStr__FP8low_typeP9_SDS_TYPEP9SPEC_TYPEPs - ==> 0, 1 for no room
long	SpcElemFirstOccArr(low_type* low, short* flags, POINTS_GROUP* group, UByte mark);	// ROM 0x0032db0c SpcElemFirstOccArr__FP8low_typePsP12POINTS_GROUPUc - ==> the index, -2 none
long	ApprHorStroke(low_type* low);								// ROM 0x0032c8a4 ApprHorStroke__FP8low_type - ==> the piece, -2 none
long	InvTanDel(low_type* low, short a, short b);					// ROM 0x0032bd80 InvTanDel__FP8low_typesT2
long	Oracle(low_type* low, PS_point_type* measures, long kind);	// ROM 0x0032be28 Oracle__FP8low_typeP13PS_point_type15_HAT_DENOM_TYPE
long	SCutFiltr(low_type* low, short* heights, SPEC_TYPE* elem, PS_point_type* p, short* dist);	// ROM 0x0032ad1c SCutFiltr__FP8low_typePsP9SPEC_TYPEP13PS_point_typeT2
long	RDFiltr(low_type* low, PS_point_type* measures, SPEC_TYPE* elem, PS_point_type* p);	// ROM 0x0032ae00 RDFiltr__FP8low_typeP13PS_point_typeP9SPEC_TYPET2
long	LeFiltr(low_type* low, SPEC_TYPE* elem, short s);			// ROM 0x0032b494 LeFiltr__FP8low_typeP9SPEC_TYPEs
long	LowStFiltr(low_type* low, short* heights, SPEC_TYPE* bar, PS_point_type* p, SPEC_TYPE* measures);	// ROM 0x0032aa38 LowStFiltr__FP8low_typePsP9SPEC_TYPEP13PS_point_typeT3
long	HatDenAnal(low_type* low, SPEC_TYPE* bar, SPEC_TYPE* stroke);	// ROM 0x0032bfa0 HatDenAnal__FP8low_typeP9SPEC_TYPET2 - ==> 2 moved, 1 not
long	ShiftsAnalyse(low_type* low, SPEC_TYPE* bar, SPEC_TYPE* stick, SPEC_TYPE* stroke);	// ROM 0x0032c484 ShiftsAnalyse__FP8low_typeP9SPEC_TYPEN22
long	DrawCross(low_type* low, short* heights, PS_point_type* p, SPEC_TYPE* bar, SPEC_TYPE* measures);	// ROM 0x0032c68c DrawCross__FP8low_typePsP13PS_point_typeP9SPEC_TYPET4
long	InsertBreakAfter(low_type* low, short marker, short at, PS_point_type* p);	// ROM 0x0032c184 InsertBreakAfter__FP8low_typesT2P13PS_point_type
long	StrokeAnalyse(low_type* low, short* heights, SPEC_TYPE* bar, SPEC_TYPE* stroke, SPEC_TYPE* measures, ULong strict);	// ROM 0x0032b57c StrokeAnalyse__FP8low_typePsP9SPEC_TYPEN23Ui - ==> 7 a stick, 2 a hatch, 1 no room
long	RMinCalc(low_type* low, short* heights, SPEC_TYPE* bar, SPEC_TYPE* measures, SPEC_TYPE* stroke, SPEC_TYPE* out);	// ROM 0x0032ae94 RMinCalc__FP8low_typePsP9SPEC_TYPEN33
long	HatchureS(low_type* low, SPEC_TYPE* elem, short* heights);	// ROM 0x0032a14c HatchureS__FP8low_typeP9SPEC_TYPEPs - ==> 7 a stick, 2 a hatch, 0 neither, 1 no room
void	FillCross(low_type* low, SPEC_TYPE* elem);				// ROM 0x00329cc4 FillCross__FP8low_typeP9SPEC_TYPE
long	FantomSt(short* count, short* x, short* y, low_buffer* bufX, low_buffer* bufY, short iBeg, short iEnd, UByte mark);	// ROM 0x0032e78c FantomSt__FPsN21P9BUF_DESCRT4sT6Uc
long	Recount(low_type* low);										// ROM 0x0032be74 Recount__FP8low_type
long	Pict(low_type* low);										// ROM 0x003298d8 Pict__FP8low_type - ==> 0, 1 for no room

// The corners (LowAngles.cpp).
long	cos_vect(long a, long b, long c, long d, short* x, short* y);	// ROM 0x00307ba8 cos_vect__FiN31PsT5
long	angle_direction(short dx, short dy, short slope);			// ROM 0x002aa418 angle_direction__FsN21 - 0x10, 0x20, 0x40 or 0x80
long	store_angle(low_type* low, short i, short k, short start, short end, short best);	// ROM 0x002aa2f0 store_angle__FP8low_typesN42 - ==> 0, 1 for no room
long	angl(low_type* low);										// ROM 0x002a9f7c angl__FP8low_type - ==> 0, 1 for no room

// The circles (LowCircle.cpp).
short	SlopeShiftDx(short dy, long slope);							// ROM 0x00307e58 SlopeShiftDx__Fsi - how far the slant (hundredths) moves a point dy across, rounded
long	Circle(low_type* low);										// ROM 0x002bc5b8 Circle__FP8low_type - the loops closed, marked as crossing pairs (6, 'c' and 'd'); ==> 0, 1 for no room

// The side extrema (LowSide.cpp).
long	brk_right(short* y, long i, long j);						// ROM 0x00305fd4 brk_right__FPsiT2 - the first pen-up from i to j, j + 1 for none
long	brk_left(short* y, long i, long j);							// ROM 0x00305ffc brk_left__FPsiT2 - the last pen-up from i back to j, j - 1 for none
long	TriangleSquare(short* x, short* y, long a, long b, long c);	// ROM 0x00307820 TriangleSquare__FPsT1iN23 - the signed area of triangle a, b, c
long	ClosedSquare(short* x, short* y, long i, long j, short* flag);	// ROM 0x00307744 ClosedSquare__FPsT1iT3T1 - the area the trace from i to j closes with its chord
long	IsTriangledPath(short* x, short* y, long i, long j, long k);	// ROM 0x00306524 IsTriangledPath__FPsT1iN23
long	iMostCurvedPoint(short* x, short* y, long i, long j, long sgn);	// ROM 0x00306c44 iMostCurvedPoint__FPsT1iN23
long	SideExtr(short* x, short* y, long i, long j, long slope, short* x0, short* y0, short* map, long* k, ULong strict);	// ROM 0x00306734 SideExtr__FPsT1iN23N31PiUi - 1/3 a bend to the left, 2/4 to the right, 0 none
long	FindSideExtr(low_type* low);								// ROM 0x00303584 FindSideExtr__FP8low_type - ==> 1

// The crossings (LowCross.cpp).
long	Cross(low_type* low);										// ROM 0x002c8fb4 Cross__FP8low_type - the trace's crossings marked as pairs (6, 9 coming back along itself, 0xa with a dash); ==> 0, 1 for no room

// The codes (LowBegin.cpp).
long	nobrk_left(short* y, long i, long j);						// ROM 0x00306024 nobrk_left__FPsiT2
long	nobrk_right(short* y, long i, long j);						// ROM 0x0030604c nobrk_right__FPsiT2
long	MidPointHeight(SPEC_TYPE* elem, low_type* low);				// ROM 0x00306f00 MidPointHeight__FP9SPEC_TYPEP8low_type
short	extremum(UByte kind, short i, short j, short* y);			// ROM 0x002f9dd8 extremum__FUcsT2Ps
long	delta_interval(short* x, short* y, long i0, long i1, long k, long slope, long* sx, long* sy, long* count, ULong trim);	// ROM 0x00308de4 delta_interval__FPsT1iN33PlN27Ui
long	DefineWritingStep(low_type* low, short* step, ULong adjust);	// ROM 0x0030845c DefineWritingStep__FP8low_typePsUi
long	init_proc_XT_ST_CROSS(low_type* low);						// ROM 0x002f8df8 init_proc_XT_ST_CROSS__FP8low_type
long	process_ZZ(low_type* low);									// ROM 0x002f9100 process_ZZ__FP8low_type
long	process_AN(low_type* low);									// ROM 0x002f9690 process_AN__FP8low_type
long	process_curves(low_type* low);								// ROM 0x002f9bc8 process_curves__FP8low_type
long	lk_begin(low_type* low);									// ROM 0x002f8d68 lk_begin__FP8low_type - the elements given their codes; ==> 0, 1 for a failure

// The i/u bottoms (LowAdjust.cpp).
void	Adjust_I_U(low_type* low);									// ROM 0x00303038 Adjust_I_U__FP8low_type - a narrow bottom between two tops recoded round (8) or sharp (7)

// lk_cross: what each crossing is (LowLkCross.cpp).  CrossInfoType is
// what analize_circles knows of a loop (ROM 0x3c bytes; it holds
// pointers, so it is cleared by its host size).
struct CrossInfoType
{
	SPEC_TYPE*		elem;			// +00  the crossing's first element
	low_type*		low;			// +04
	SPEC_TYPE*		lower;			// +08  the last lower extremum inside the loop
	long			cosine;			// +0c  between the two passes (hundredths)
	_RECT			box;			// +10  the loop's box (count_cross_box)
	short			dx;				// +18
	short			dy;				// +1a
	long			maxDx;			// +1c  the loop's widest (GetMaxDxInGamma)
	long			midX;			// +20  the middle of the two passes
	long			midY;			// +24
	long			loop;			// +28  the two passes' length against their span, percent
	long			boxMidX;		// +2c
	long			boxMidY;		// +30
	long			relX;			// +34  the middle across the box, percent
	long			relY;			// +38  and down it
};
void	count_cross_box(SPEC_TYPE* e, short* x, short* y, _RECT* box, short* dx, short* dy);	// ROM 0x002ca310 count_cross_box__FP9SPEC_TYPEPsT2P5_RECTN22
void	FillCrossInfo(low_type* low, SPEC_TYPE* e, CrossInfoType* ci);	// ROM 0x002cda64 FillCrossInfo__FP8low_typeP9SPEC_TYPEP13CrossInfoType
long	CheckSmallGamma(CrossInfoType* ci);							// ROM 0x002ca3b0 CheckSmallGamma__FP13CrossInfoType
long	Isgammathin(CrossInfoType* ci, SPEC_TYPE* ext);				// ROM 0x002ca4d4 Isgammathin__FP13CrossInfoTypeP9SPEC_TYPE
long	GetMaxDxInGamma(long a, long b, long c, short* x, short* y, UByte kind, long* left, long* right);	// ROM 0x002cd63c GetMaxDxInGamma__FiN21PsT4UcPiT7
long	IsEndOfStrokeInsideCross(CrossInfoType* ci);					// ROM 0x002cd9a4 IsEndOfStrokeInsideCross__FP13CrossInfoType
void	Decision_GU_or_O_(CrossInfoType* ci);						// ROM 0x002cd860 Decision_GU_or_O___FP13CrossInfoType
SPEC_TYPE*	SkipAnglesAfter(SPEC_TYPE* e);							// ROM 0x00305b0c SkipAnglesAfter__FP9SPEC_TYPE
SPEC_TYPE*	SkipAnglesBefore(SPEC_TYPE* e);							// ROM 0x00305b48 SkipAnglesBefore__FP9SPEC_TYPE
long	iXYweighted_max_right(short* x, short* y, long i, long lim, long wx, long wy);	// ROM 0x00307154 iXYweighted_max_right__FPsT1iN33
long	cos_normalslope(long i, long j, long slope, short* x, short* y);	// ROM 0x00307c74 cos_normalslope__FiN21PsT4
long	IsRightGulfLikeIn3(short* x, short* y, long i, long j, long* at);	// ROM 0x00308298 IsRightGulfLikeIn3__FPsT1iT3Pi
long	IsPointOnBorder(short* xs, short* ys, long i, long j, short px, short py, ULong* cross);	// ROM 0x003096fc IsPointOnBorder__FPsT1iT3sT5PUi
long	IsPointInsideArea(short* xs, short* ys, long n, short px, short py, short* where);	// ROM 0x003092cc IsPointInsideArea__FPsT1isT4T1 - *where 0 on the border, 1 inside, 2 outside
long	IsShapeDUR(SPEC_TYPE* p, SPEC_TYPE* q, SPEC_TYPE* r, SPEC_TYPE* stick, low_type* low);	// ROM 0x002cbbe0 IsShapeDUR__FP9SPEC_TYPEN31P8low_type
long	IsDUR(SPEC_TYPE* e, SPEC_TYPE* a, SPEC_TYPE* b, low_type* low);	// ROM 0x002cb58c IsDUR__FP9SPEC_TYPEN21P8low_type
long	is_DDL(SPEC_TYPE* e, SPEC_TYPE* upper, low_type* low);		// ROM 0x002cb25c is_DDL__FP9SPEC_TYPET1P8low_type
void	check_IU_ID_in_crossing(SPEC_TYPE** pe, short* x, short* y);	// ROM 0x002cbcbc check_IU_ID_in_crossing__FPP9SPEC_TYPEPsT2
void	Restore_AN(low_type* low, SPEC_TYPE* e, UByte kind, short mode);	// ROM 0x002cac98 Restore_AN__FP8low_typeP9SPEC_TYPEUcs
long	IsOutsideOfCrossing(SPEC_TYPE* e, SPEC_TYPE* r, SPEC_TYPE* partner, low_type* low, SPEC_TYPE** prev, SPEC_TYPE** moved, ULong* flag);	// ROM 0x002cada0 IsOutsideOfCrossing__FP9SPEC_TYPEN21P8low_typePP9SPEC_TYPET5PUi
void	CheckInsideCrossing(SPEC_TYPE* e, SPEC_TYPE* r, short* count);	// ROM 0x002caf8c CheckInsideCrossing__FP9SPEC_TYPET1Ps
long	IsInnerAngle(short* x, short* y, SPEC_TYPE* partner, SPEC_TYPE* e, SPEC_TYPE* r);	// ROM 0x002cb138 IsInnerAngle__FPsT1P9SPEC_TYPEN23
long	del_inside_circles(low_type* low);							// ROM 0x002ca8a0 del_inside_circles__FP8low_type - ==> 0
SPEC_TYPE*	cross_little(SPEC_TYPE* e);								// ROM 0x002cca28 cross_little__FP9SPEC_TYPE
long	EndIUIDNearStick(SPEC_TYPE* end, SPEC_TYPE* partner, short* x, short* y);	// ROM 0x002ca0a0 EndIUIDNearStick__FP9SPEC_TYPET1PsT3
long	analize_sticks(low_type* low);								// ROM 0x002cbe54 analize_sticks__FP8low_type - ==> 0
long	analize_circles(low_type* low);								// ROM 0x002cca44 analize_circles__FP8low_type - ==> 0
long	lk_cross(low_type* low);									// ROM 0x002ca074 lk_cross__FP8low_type - ==> 0

// lk_duga's passes (LowLkDuga.cpp).  NxtPrvCircle_type is what the
// passes over a loop's neighbours share (ROM 0x18 bytes; it holds
// pointers, so it is cleared by its host size).
struct NxtPrvCircle_type
{
	SPEC_TYPE*		e;				// +00  the loop
	SPEC_TYPE**		pNext;			// +04  the element after it (the caller's variable)
	SPEC_TYPE**		pPrev;			// +08  and before it
	low_type*		low;			// +0c
	UByte*			pHeight;		// +10  the loop's height band (the caller's byte)
	UByte			band;			// +14  its band (attr & 0x30)
	UByte			n21;			// +15  the 0x21s inside it (other 0x40) before it
	UByte			n22;			// +16  and the 0x22s
};
long	lk_duga(low_type* low);										// ROM 0x002fa2f8 lk_duga__FP8low_type - ==> 0
void	prevent_arcs(low_type* low);								// ROM 0x002fd3a8 prevent_arcs__FP8low_type
long	conv_sticks_to_arcs(low_type* low);							// ROM 0x002fcbb8 conv_sticks_to_arcs__FP8low_type - ==> 0
long	delete_UD_before_DDL(low_type* low);						// ROM 0x002fc70c delete_UD_before_DDL__FP8low_type - ==> 0
long	del_before_after_circles(low_type* low);					// ROM 0x002fd474 del_before_after_circles__FP8low_type - ==> 0
void	make_CDL_in_O_GU_f(SPEC_TYPE* e, SPEC_TYPE* n, UByte band);	// ROM 0x002fc764 make_CDL_in_O_GU_f__FP9SPEC_TYPET1Uc
SPEC_TYPE*	del_prv_and_shift(SPEC_TYPE* e);						// ROM 0x002fb3e0 del_prv_and_shift__FP9SPEC_TYPE - ==> the element before
long	check_inside_circle(SPEC_TYPE* e, SPEC_TYPE* t, low_type* low);	// ROM 0x002fd7ac check_inside_circle__FP9SPEC_TYPET1P8low_type
long	change_circle_before(NxtPrvCircle_type* s, UByte h);		// ROM 0x002fb148 change_circle_before__FP17NxtPrvCircle_typeUc
long	change_and_del_before_circle(NxtPrvCircle_type* s, UByte h);	// ROM 0x002fb29c change_and_del_before_circle__FP17NxtPrvCircle_typeUc
long	Is_8(short* x, short* y, SPEC_TYPE* a, SPEC_TYPE* b);		// ROM 0x002fb70c Is_8__FPsT1P9SPEC_TYPET3
long	UpElemBeforeCircle(NxtPrvCircle_type* s, UByte h);			// ROM 0x002fb3fc UpElemBeforeCircle__FP17NxtPrvCircle_typeUc
long	DnElemBeforeCircle(NxtPrvCircle_type* s, UByte h);			// ROM 0x002fb7f0 DnElemBeforeCircle__FP17NxtPrvCircle_typeUc
long	check_before_circle(NxtPrvCircle_type* s);					// ROM 0x002fb108 check_before_circle__FP17NxtPrvCircle_type - ==> 0
long	check_after_circle(NxtPrvCircle_type* s);					// ROM 0x002fbaec check_after_circle__FP17NxtPrvCircle_type - ==> 0
long	check_next_for_circle(NxtPrvCircle_type* s);				// ROM 0x002fbb48 check_next_for_circle__FP17NxtPrvCircle_type - ==> 0
long	check_next_for_common(NxtPrvCircle_type* s);				// ROM 0x002fbdec check_next_for_common__FP17NxtPrvCircle_type - ==> 0
long	change_circle_after(NxtPrvCircle_type* s, UByte nBand, UByte nh);	// ROM 0x002fbfb4 change_circle_after__FP17NxtPrvCircle_typeUcT2
long	check_next_for_special(NxtPrvCircle_type* s);				// ROM 0x002fc0a4 check_next_for_special__FP17NxtPrvCircle_type - ==> 0
long	check_before_after_GU(NxtPrvCircle_type* s);				// ROM 0x002fc414 check_before_after_GU__FP17NxtPrvCircle_type - ==> 0
long	O_GU_To3Elements(NxtPrvCircle_type* s);						// ROM 0x002fc538 O_GU_To3Elements__FP17NxtPrvCircle_type - ==> 1 when done
long	IsTipBefore(NxtPrvCircle_type* s);							// ROM 0x002fba20 IsTipBefore__FP17NxtPrvCircle_type
long	IsDx_Dy_in_arcs_OK(SPEC_TYPE* e, SPEC_TYPE* t, long lim, short* x, short* y);	// ROM 0x002fb96c IsDx_Dy_in_arcs_OK__FP9SPEC_TYPET1iPsT4
long	IsDx_Dy_in_tips_OK(SPEC_TYPE* e, SPEC_TYPE* t, long lim, short* x, short* y);	// ROM 0x002fc7c8 IsDx_Dy_in_tips_OK__FP9SPEC_TYPET1iPsT4
long	IsTipOK(SPEC_TYPE* e, SPEC_TYPE* t, short* x);				// ROM 0x002fc8d8 IsTipOK__FP9SPEC_TYPET1Ps
long	DyLimit(low_type* low, SPEC_TYPE* e, SPEC_TYPE* a, SPEC_TYPE* b, SPEC_TYPE* c, long k);	// ROM 0x002fc954 DyLimit__FP8low_typeP9SPEC_TYPEN32i - ==> the limit, -1 for no tip
long	arcs_processing(low_type* low);								// ROM 0x002fa358 arcs_processing__FP8low_type - ==> 0
long	ins_third_elem_in_circle(SPEC_TYPE* e, low_type* low);		// ROM 0x002fab84 ins_third_elem_in_circle__FP9SPEC_TYPEP8low_type
long	delete_CROSS_elements(low_type* low);						// ROM 0x002fab00 delete_CROSS_elements__FP8low_type - ==> 0
long	check_IUb_IDf_small(low_type* low);							// ROM 0x002fae80 check_IUb_IDf_small__FP8low_type - ==> 0

// The colons and the side bends found late (LowRestore.cpp).
void	AdjustBegEndWithoutPoint(SPEC_TYPE* e);						// ROM 0x00305044 AdjustBegEndWithoutPoint__FP9SPEC_TYPE
Boolean	LooksLikeIAndPoint(SPEC_TYPE* dot, long p, short dx, short* x, short* y);	// ROM 0x00304964 LooksLikeIAndPoint__FP9SPEC_TYPEisPsT4
long	PutColonAtItsPlace(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b);	// ROM 0x00304a34 PutColonAtItsPlace__FP8low_typeP9SPEC_TYPET2 - ==> 1, 0 for a failure
long	RestoreColons(low_type* low);								// ROM 0x003044d8 RestoreColons__FP8low_type - ==> 0
SPEC_TYPE*	SkipRealAnglesAndPointsAfter(SPEC_TYPE* e);				// ROM 0x003042a4 SkipRealAnglesAndPointsAfter__FP9SPEC_TYPE
SPEC_TYPE*	SkipRealAnglesAndPointsBefore(SPEC_TYPE* e);			// ROM 0x003042e0 SkipRealAnglesAndPointsBefore__FP9SPEC_TYPE
Boolean	IsSmthRelevant_InBetween(SPEC_TYPE* a, SPEC_TYPE* b, long lo, long hi);	// ROM 0x0030446c IsSmthRelevant_InBetween__FP9SPEC_TYPET1iT3
long	PostFindSideExtr(low_type* low);							// ROM 0x00303718 PostFindSideExtr__FP8low_type - ==> 1

// An xr (ROM 0x18 bytes, no pointers, so the same on the host): one
// element of what the low level hands the reader.  The halfwords are
// kept as the ROM keeps them, big-endian, and read and written through
// XrGetH/XrSetH, because the ROM writes them a byte at a time.
struct xrd_el_type
{
	UByte			type;			// +00  what it is (exchange's codes: 1 a break, 2..4 a stroke's end, 6..0x1f extrema, 0x20..0x2c arcs and angles, 0x2d.. crossings and marks; 0 ends the list)
	UByte			attrib;			// +01  1 last in its letter (MarkXrAsLastInLetter), 2 a stroke start with a gap, 0x80 next to a break
	UByte			penalty;		// +02  (AssignInputPenaltyAndStrict)
	UByte			height;			// +03  the height band (exchange); FillSHR writes over it with its own class of the piece's height
	UByte			shift;			// +04  (FillSHR)
	UByte			orient;			// +05  its direction, 0..0x1f (FillOrients)
	UByte			link;			// +06  how it joins the next (GetLinkBetweenThisAndNextXr)
	UByte			f07;
	UByte			hotpoint[2];	// +08  the point it is at (in the original trace)
	UByte			begpoint[2];	// +0a
	UByte			endpoint[2];	// +0c
	UByte			box[8];			// +0e  left, top, right, bottom
	UByte			f16[2];
};

inline short	XrGetH(const UByte* h)			{ return (short) ((h[0] << 8) | h[1]); }
inline void		XrSetH(UByte* h, long v)		{ h[0] = (UByte) (v >> 8); h[1] = (UByte) v; }
enum { kXrLeft = 0, kXrTop = 2, kXrRight = 4, kXrBottom = 6 };	// the halfwords of box
enum { kXrMaxElements = 0x78 };									// the room exchange assumes

// A stroke description's tail as CalculateStickOrArc reads it (ROM
// SDB_TYPE): the _SDS_TYPE from its chord on.
struct SDB_TYPE
{
	short			chord;			// +00 (_SDS_TYPE +0x10)
	short			slope;			// +02
	short			distA;			// +04
	short			iA;				// +06
	short			distB;			// +08
	short			iB;				// +0a
	short			maxDist;		// +0c
	short			iMax;			// +0e
	int32_t			length;			// +10
	short			crook;			// +14
};

// exchange: the special elements written as xrs (LowExchange.cpp).
extern const UByte	penlDefX[64];		// the penalty for each xr type
extern const UByte	penlDefH[16];		// and what each height band adds
extern const short	xr_type_merits[64];	// each xr type's properties: 1 lower, 2 upper, 0x10 a stroke end, 0x20 a crossing, 0x40 a link, 0x80 ..., 0x100 an arc
extern const int	ratio_to_angle[8];	// the slopes (hundredths) the eight angles of a quadrant begin at
extern const int	kSHRRatioLimits[16];	// FillSHR's height-ratio classes (percent)
extern const int	kSHRShiftLimits[16];	// and its shift classes
long	exchange(low_type* low, xrdata_type* xr);					// ROM 0x002c7a00 exchange__FP8low_typeP11xrdata_type - ==> 0
Boolean	X_IsBreak(xrd_el_type* xr);									// ROM 0x00305c68 X_IsBreak__FP11xrd_el_type - types 1..4
Boolean	IsStrongElem(SPEC_TYPE* elem);								// ROM 0x00305bfc IsStrongElem__FP9SPEC_TYPE
long	AssignInputPenaltyAndStrict(SPEC_TYPE* elem, xrd_el_type* xr);	// ROM 0x002c854c AssignInputPenaltyAndStrict__FP9SPEC_TYPEP11xrd_el_type
long	MarkXrAsLastInLetter(xrd_el_type* xr, low_type* low, SPEC_TYPE* elem);	// ROM 0x002c8be0 MarkXrAsLastInLetter__FP11xrd_el_typeP8low_typeP9SPEC_TYPE - ==> 0
long	check_xrdata(xrd_el_type* xr, low_type* low);				// ROM 0x002c8874 check_xrdata__FP11xrd_el_typeP8low_type - ==> 0, 1 for no room
Boolean	PutZintoXrd(low_type* low, xrd_el_type* dst, xrd_el_type* prev, xrd_el_type* cur, UByte penalty, short i, short* count);	// ROM 0x002c8a90 PutZintoXrd__FP8low_typeP11xrd_el_typeN22UcsPs - ==> whether the room is used up
long	GetLinkBetweenThisAndNextXr(low_type* low, SPEC_TYPE* elem, xrd_el_type* xr);	// ROM 0x000fe384 GetLinkBetweenThisAndNextXr__FP8low_typeP9SPEC_TYPEP11xrd_el_type
long	GetMovementLink(UByte code);								// ROM 0x000fe55c GetMovementLink__FUc
long	GetCurveLink(short crook, ULong right);						// ROM 0x000fe594 GetCurveLink__FsUi
long	CalculateStickOrArc(SDB_TYPE* sdb);							// ROM 0x000fe60c CalculateStickOrArc__FP8SDB_TYPE
long	CalculateLinkLikeSZ(SDB_TYPE* sdb, long dy);				// ROM 0x000fe6d8 CalculateLinkLikeSZ__FP8SDB_TYPEi
long	CalculateLinkWithoutSDS(low_type* low, SPEC_TYPE* elem, SPEC_TYPE* next);	// ROM 0x000fe754 CalculateLinkWithoutSDS__FP8low_typeP9SPEC_TYPET2

// The xrs' features (LowXrFeatures.cpp).
struct vect_type
{
	long			a;				// +00  the point it starts from
	long			b;				// +04  and the one it looks towards
	long			blp;			// +08  a point it must not pass (GetBlp)
	long			x1, y1;			// +0c
	long			x2, y2;			// +14
};
long	GetXrHT(xrd_el_type* xr);									// ROM 0x0027e508 GetXrHT__FP11xrd_el_type - 2 lower, 1 upper, 4 a stroke end, 0
long	GetXrMovable(xrd_el_type* xr);								// ROM 0x0027e540 GetXrMovable__FP11xrd_el_type
long	GetAngle(long dx, long dy);									// ROM 0x0027e568 GetAngle__FiT1 - 0..0x1f
long	GetVect(long forward, vect_type* v, PS_point_type* trace, long n, long dist);	// ROM 0x0027e638 GetVect__FiP9vect_typeP13PS_point_typeN21
long	GetBlp(long forward, vect_type* v, long i, xrdata_type* xr);	// ROM 0x0027e7a8 GetBlp__FiP9vect_typeT1P11xrdata_type
Boolean	IsXrLink(xrd_el_type* xr);									// ROM 0x0027e840 IsXrLink__FP11xrd_el_type
long	GetXrMetrics(xrd_el_type* xr);								// ROM 0x0027e868 GetXrMetrics__FP11xrd_el_type
long	FillXrFeatures(xrdata_type* xr, low_type* low);				// ROM 0x0027e880 FillXrFeatures__FP11xrdata_typeP8low_type
long	GetCurSlope(long n, PS_point_type* trace);					// ROM 0x0027e8fc GetCurSlope__FiP13PS_point_type - the writing's slant in hundredths
long	FillSHR(long slope, xrdata_type* xr, low_type* low);		// ROM 0x0027ea0c FillSHR__FiP11xrdata_typeP8low_type - ==> 0, 1 for fewer than three
long	FillOrients(long slope, xrdata_type* xr, low_type* low);	// ROM 0x0027f58c FillOrients__FiP11xrdata_typeP8low_type - ==> 0

// xt_st_zz's passes: the late strokes placed, the breaks weighed
// (LowXtSt.cpp).
long	xt_st_zz(low_type* low);									// ROM 0x002af7ac xt_st_zz__FP8low_type - ==> 0
long	conv_top_elem_to_ST(low_type* low);							// ROM 0x002af89c conv_top_elem_to_ST__FP8low_type - ==> 0
long	Placement_XT_CUTTED(SPEC_TYPE* e, low_type* low);			// ROM 0x002afa84 Placement_XT_CUTTED__FP9SPEC_TYPEP8low_type - ==> 0
long	SortXT_ST(low_type* low);									// ROM 0x002afb9c SortXT_ST__FP8low_type - ==> 0
long	GetStrokeWhichBelongsToRestricted(SPEC_TYPE* e, low_type* low, short* beg, short* end);	// ROM 0x002afce4 GetStrokeWhichBelongsToRestricted__FP9SPEC_TYPEP8low_typePsT3 - ==> 0
long	FindDelayedStroke(low_type* low);							// ROM 0x002afda4 FindDelayedStroke__FP8low_type - ==> 0
long	CheckStrokesForDxTimeMatch(low_type* low);					// ROM 0x002affdc CheckStrokesForDxTimeMatch__FP8low_type - ==> 0
long	placement_X(low_type* low);									// ROM 0x002b0298 placement_X__FP8low_type - ==> 0
long	FindMisplacedParentheses(low_type* low);					// ROM 0x002b05a8 FindMisplacedParentheses__FP8low_type - ==> 0
long	find_CROSS(low_type* low, short iBeg, short iEnd, SPEC_TYPE** cross);	// ROM 0x002b0780 find_CROSS__FP8low_typesT2PP9SPEC_TYPE - ==> 1 found
long	find_umlaut(low_type* low);									// ROM 0x002b083c find_umlaut__FP8low_type - ==> 0
long	IsPartOfTrajectoryInside(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b);	// ROM 0x002b0b24 IsPartOfTrajectoryInside__FP8low_typeP9SPEC_TYPET2
long	CalcDistBetwXr(short* x, short* y, long a0, long a1, long b0, long b1, short* flag);	// ROM 0x0030857c CalcDistBetwXr__FPsT1iN33T1
long	del_close_MAX_MIN(low_type* low);							// ROM 0x002b0ca0 del_close_MAX_MIN__FP8low_type - ==> 0
long	IsExclamationOrQuestionSign(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b);	// ROM 0x002b12dc IsExclamationOrQuestionSign__FP8low_typeP9SPEC_TYPET2
long	del_ZZ_HATCH(SPEC_TYPE* head);								// ROM 0x002b13dc del_ZZ_HATCH__FP9SPEC_TYPE - ==> 0
long	punctuation(low_type* low, SPEC_TYPE* start, SPEC_TYPE* e);	// ROM 0x002b1494 punctuation__FP8low_typeP9SPEC_TYPET2 - ==> 1 punctuation
void	insert_drop(SPEC_TYPE* e, low_type* low);					// ROM 0x002b19a8 insert_drop__FP9SPEC_TYPEP8low_type
long	FindQuotes(SPEC_TYPE* e, low_type* low);					// ROM 0x002b1a48 FindQuotes__FP9SPEC_TYPEP8low_type - ==> 1 a quote
void	PutLeadingQuotes(low_type* low, SPEC_TYPE* a, SPEC_TYPE* b);	// ROM 0x002b1eb0 PutLeadingQuotes__FP8low_typeP9SPEC_TYPET2
void	PutTrailingQuotes(low_type* low, SPEC_TYPE* a);				// ROM 0x002b1f14 PutTrailingQuotes__FP8low_typeP9SPEC_TYPE
long	IsStick(SPEC_TYPE* a, SPEC_TYPE* b);						// ROM 0x002b1fcc IsStick__FP9SPEC_TYPET1
long	is_X_crossing_XT(SPEC_TYPE* e, low_type* low, UByte* next);	// ROM 0x002b2040 is_X_crossing_XT__FP9SPEC_TYPEP8low_typePUc
void	change_last_IU_height(low_type* low);						// ROM 0x002b230c change_last_IU_height__FP8low_type
long	placement_XT_ST(low_type* low);								// ROM 0x002b241c placement_XT_ST__FP8low_type - ==> 0
long	make_different_breaks(low_type* low);						// ROM 0x002b26b8 make_different_breaks__FP8low_type - ==> 0
long	GetTraceBoxInsideYZone(short* x, short* y, long iBeg, long iEnd, short yTop, short yBottom, _RECT* box, short* iRight, short* iLeft, short* iBottom, short* iTop);	// ROM 0x00308050 GetTraceBoxInsideYZone__FPsT1iT3sT5P5_RECTN41 - ==> 0 for none in the zone
long	GetDxBetweenStrokes(low_type* low, long a0, long a1, long b0, long b1);	// ROM 0x002b2df0 GetDxBetweenStrokes__FP8low_typeiN32
long	SecondHigherFirst(low_type* low, SPEC_TYPE* brk, SPEC_TYPE* a, SPEC_TYPE* b, long a0, long a1, long b0, long b1);	// ROM 0x002b348c SecondHigherFirst__FP8low_typeP9SPEC_TYPEN22iN35
long	AdjustZZ_BegEnd(low_type* low);								// ROM 0x002b377c AdjustZZ_BegEnd__FP8low_type - ==> 0
void	redirect_sticks(low_type* low);								// ROM 0x002b3900 redirect_sticks__FP8low_type
long	find_angstrem(low_type* low);								// ROM 0x002b3ac0 find_angstrem__FP8low_type - ==> 0
long	CheckSequenceOfElements(low_type* low);						// ROM 0x002b3e4c CheckSequenceOfElements__FP8low_type - ==> 0
long	iClosestToY(short* y, long i, long j, short v);				// ROM 0x0030769c iClosestToY__FPsiT2s - ==> -1 for none
long	Put_XT_ST(low_type* low, SPEC_TYPE* best, SPEC_TYPE* e, ULong found);	// ROM 0x002b4fbc Put_XT_ST__FP8low_typeP9SPEC_TYPET2Ui - ==> 0
SPEC_TYPE*	FindClosestUpperElement(SPEC_TYPE* head, short i);		// ROM 0x002b4634 FindClosestUpperElement__FP9SPEC_TYPEs
long	DoubleXT(SPEC_TYPE* e, low_type* low);						// ROM 0x002b44e8 DoubleXT__FP9SPEC_TYPEP8low_type - ==> 1 doubled
long	Placement_XT_With_HATCH(SPEC_TYPE* e, SPEC_TYPE* r, low_type* low);	// ROM 0x002b4060 Placement_XT_With_HATCH__FP9SPEC_TYPET1P8low_type - ==> 0
long	Placement_XT_WO_HATCH_AND_ST(SPEC_TYPE* e, low_type* low);	// ROM 0x002b46cc Placement_XT_WO_HATCH_AND_ST__FP9SPEC_TYPEP8low_type - ==> 0
long	IsNearI(SPEC_TYPE* e);										// ROM 0x002d98c8 IsNearI__FP9SPEC_TYPE - the top of an i
long	RestoreApostroph(low_type* low, SPEC_TYPE* e);				// ROM 0x002d8b38 RestoreApostroph__FP8low_typeP9SPEC_TYPE - ==> 1 an apostrophe

// FindDArcs: the arcs of an S or a Z, and a d's bowl, found between an
// upper element and the lower one after it (LowDArcs.cpp).  The ROM keeps
// the pair's description on FindDArcs' stack (0x44 bytes); the offsets are
// the ROM's.
struct SZD_FEATURES
{
	low_type*		low;			// +00
	SPEC_TYPE*		e1;				// +04  the upper element
	SPEC_TYPE*		e2;				// +08  and the lower one after it
	SPEC_TYPE*		fNew;			// +0c  the element put between them, if any
	short*			x;				// +10
	short*			y;				// +14
	short*			xInit;			// +18
	short*			yInit;			// +1c
	short*			map;			// +20  buffer 2: a point's index in the initial trace
	short			band1;			// +24  e1's height band
	short			band2;			// +26
	short			iBeg1;			// +28  the initial trace e1 covers
	short			iEnd1;			// +2a
	short			iBeg2;			// +2c  and e2
	short			iEnd2;			// +2e
	short			i0;				// +30  where the two arcs meet
	short			i1;				// +32  the first's top
	short			i2;				// +34  the second's bottom
	short			far1;			// +36  each arc's point furthest from its chord
	short			far2;			// +38
	short			curv1;			// +3a  each arc's bend (CurvNonQuadr)
	short			curv2;			// +3c
	short			dev;			// +3e  how far i0 is to the side of the line from e1's start to e2's end
	long			back;			// +40  1 when CheckBackDArcs found the stroke going back on itself
};
SPEC_TYPE*	SkipAnglesAndHMoves(SPEC_TYPE* e);						// ROM 0x003015b8 SkipAnglesAndHMoves__FP9SPEC_TYPE
long	CurvNonQuadr(short* x, short* y, long i, long j);			// ROM 0x003015fc CurvNonQuadr__FPsT1iT3 - the bend in hundredths, signed, at most 1000
long	iXmax_right(short* x, short* y, long i, long dx);			// ROM 0x003071c8 iXmax_right__FPsT1iT3
long	iXmin_right(short* x, short* y, long i, long dx);			// ROM 0x0030722c iXmin_right__FPsT1iT3
Boolean	CurvLikeSZ(short a, short b, short t);						// ROM 0x00305930 CurvLikeSZ__FsN21
Boolean	LooksLikeSZ(short* x, short* y, long i, long j);			// ROM 0x003053f8 LooksLikeSZ__FPsT1iT3
Boolean	CurvConsistent(short* x, short* y, long i, long j, short* map);	// ROM 0x00305984 CurvConsistent__FPsT1iT3T1
long	FillBasicFeatures(SZD_FEATURES* f, low_type* low);			// ROM 0x0030431c FillBasicFeatures__FP12SZD_FEATURESP8low_type
long	PairWorthLookingAt(SZD_FEATURES* f);						// ROM 0x003050dc PairWorthLookingAt__FP12SZD_FEATURES
long	FillCurvFeatures(SZD_FEATURES* f);							// ROM 0x003051a4 FillCurvFeatures__FP12SZD_FEATURES
long	FillComplexFeatures(SZD_FEATURES* f);						// ROM 0x00305474 FillComplexFeatures__FP12SZD_FEATURES
long	CheckBackDArcs(SZD_FEATURES* f);							// ROM 0x003056a0 CheckBackDArcs__FP12SZD_FEATURES
long	CheckSZArcs(SZD_FEATURES* f);								// ROM 0x003016b8 CheckSZArcs__FP12SZD_FEATURES
long	CheckDArcs(SZD_FEATURES* f);								// ROM 0x0030222c CheckDArcs__FP12SZD_FEATURES
void	ArrangeAnglesNearNew(SZD_FEATURES* f);						// ROM 0x00302d74 ArrangeAnglesNearNew__FP12SZD_FEATURES
void	KillHAtNewElem(SZD_FEATURES* f);							// ROM 0x00302ed0 KillHAtNewElem__FP12SZD_FEATURES
long	FindDArcs(low_type* low);									// ROM 0x00302f00 FindDArcs__FP8low_type - ==> 0

// The filters.
void	Errorprov(low_type* low);									// ROM 0x002e0f1c Errorprov__FP8low_type - a pen-up that follows a pen-up taken out
long	Filt(low_type* low, short dist2, short mode);				// ROM 0x002e1064 Filt__FP8low_typesT2 - the trace resampled a step of about the root of dist2 apart; ==> 0
long	PreFilt(short dist2, low_type* low);						// ROM 0x002e15f4 PreFilt__FsP8low_type - the points closer than the root of dist2 to the one kept before dropped; ==> 0
long	PSProc(low_type* low, short n);								// ROM 0x002e19a0 PSProc__FP8low_types - the special elements moved to the filtered trace; ==> 1 when points were lost at the end

#endif	/* __LOWLEVEL_H */
