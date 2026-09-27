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
	UByte			f70[4];			// +70
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
void	SetXYToInitial(low_type* low);
long	BaselineAndScale(low_type* low);							// ROM 0x0034eba0 BaselineAndScale__FP8low_type - the trace filtered, its extrema found and the base line found; ==> 0, 1 for a failure
long	transfrmN(low_type* low);									// ROM 0x001baaf8 transfrmN__FP8low_type - the base-line finder: the borders found, the trace rescaled to them; ==> 0, 1 for a failure
extern const short	const1[8];										// the engine's constants: [0] the filter's scale (10), [5] the default extremum step (8)								// ROM 0x00305a14 SetXYToInitial__FP8low_type
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

// The filters.
void	Errorprov(low_type* low);									// ROM 0x002e0f1c Errorprov__FP8low_type - a pen-up that follows a pen-up taken out
long	Filt(low_type* low, short dist2, short mode);				// ROM 0x002e1064 Filt__FP8low_typesT2 - the trace resampled a step of about the root of dist2 apart; ==> 0
long	PreFilt(short dist2, low_type* low);						// ROM 0x002e15f4 PreFilt__FsP8low_type - the points closer than the root of dist2 to the one kept before dropped; ==> 0
long	PSProc(low_type* low, short n);								// ROM 0x002e19a0 PSProc__FP8low_types - the special elements moved to the filtered trace; ==> 1 when points were lost at the end

#endif	/* __LOWLEVEL_H */
