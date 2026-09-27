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
	UByte			f8;				// +08
	UByte			attr;			// +09  its stroke's ending attr
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

// The stroke descriptions' array and how full it is (ROM 12 bytes).
struct _SDS_TYPE;
struct _SDS_CONTROL_TYPE
{
	short			sizeSDS;		// +00  room for how many
	short			f02;
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
	UByte			f64[0x0a];		// +64
	short			fSlope;			// +6e  the writing's slant (from rc +0xac, back to it at the end)
	UByte			f70[4];			// +70
	_RECT			fBox;			// +74  the trace's box
	UByte			f7c[0x20];		// +7c..+9b
};

// The engine's arithmetic.
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
short	MaxPointsGrown(short n);									// ROM 0x00307f50 MaxPointsGrown__Fs
Boolean	AllocSpecl(SPEC_TYPE** specl, short n);						// ROM 0x00306410 AllocSpecl__FPP9SPEC_TYPEs
void	DeallocSpecl(SPEC_TYPE** specl);							// ROM 0x00307984 DeallocSpecl__FPP9SPEC_TYPE
long	InitSpecl(low_type* low, short n);							// ROM 0x002ba430 InitSpecl__FP8low_types - the list emptied, element 0 its head; ==> 0
Boolean	InitSpeclElement(SPEC_TYPE* elem);							// ROM 0x002ba4cc InitSpeclElement__FP9SPEC_TYPE - ==> whether elem was nil
Boolean	CreateSDS(low_type* low, short n);							// ROM 0x0032f8bc CreateSDS__FP8low_types
void	DestroySDS(low_type* low);									// ROM 0x0032f91c DestroySDS__FP8low_type

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
long	iMostFarFromChord(short* x, short* y, long i, long j);		// ROM 0x00306448 iMostFarFromChord__FPsT1iT3 - the point from i to j furthest from their chord (the middle of a run of equals)
short	NewIndex(short* index, short* y, short i, short n, short mode);	// ROM 0x00307cd0 NewIndex__FPsT1sN23 - where an old point index went after filtering: 0 the first new point from it, 2 the last, 1 between the two; -2 none

// The special elements found.
long	Extr(low_type* low, short step, short eps1, short eps2, short eps3, short depth, short flags);	// ROM 0x002ba52c Extr__FP8low_typesN52 - every stroke's extrema in the directions flags asks for; ==> 0, 1 for no room
long	MarkSpecl(low_type* low, SPEC_TYPE* elem);					// ROM 0x002bc36c MarkSpecl__FP8low_typeP9SPEC_TYPE - a copy added to the list; ==> 0, 1 for no room

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
long	extract_num_extr(low_type* low, UByte kind, EXTR* extr, long* count);	// ROM 0x001bdff0 extract_num_extr__FP8low_typeUcP4EXTRPi - ==> 0, 1 for more than 50

// The filters.
void	Errorprov(low_type* low);									// ROM 0x002e0f1c Errorprov__FP8low_type - a pen-up that follows a pen-up taken out
long	Filt(low_type* low, short dist2, short mode);				// ROM 0x002e1064 Filt__FP8low_typesT2 - the trace resampled a step of about the root of dist2 apart; ==> 0
long	PreFilt(short dist2, low_type* low);						// ROM 0x002e15f4 PreFilt__FsP8low_type - the points closer than the root of dist2 to the one kept before dropped; ==> 0
long	PSProc(low_type* low, short n);								// ROM 0x002e19a0 PSProc__FP8low_types - the special elements moved to the filtered trace; ==> 1 when points were lost at the end

#endif	/* __LOWLEVEL_H */
