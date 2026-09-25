/*
	File:		recognition/ShapeGeometry.h

	Contains:	What the shape domain's Classify is made of: the fitting of
				a shape's key points, the ellipses, the equations its
				symmetries are written as and their solution, and the
				snapping of its ends.  See ShapeDomain.h for the order they
				run in.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __SHAPEGEOMETRY_H
#define __SHAPEGEOMETRY_H

#ifndef __SHAPEDOMAIN_H
#include "ShapeDomain.h"
#endif

#include <stdint.h>

// A run of samples closer together than the small distance (RSmallDists):
// the index it starts and ends at; -1 -1 ends the list, a start of -2 says
// the runs are not to be trusted.
struct Run
{
	int32_t		fStart;
	int32_t		fEnd;
};

// One piece of a shape's outline as a cubic in Hermite form (0x20 bytes):
// its two ends and the tangents there.
struct SplineSeg
{
	FPoint		fP0;			// +0x00
	FPoint		fP1;			// +0x08
	FPoint		fT0;			// +0x10
	FPoint		fT1;			// +0x18
};

extern const FPoint	ptZero;								// ROM 0x0c104d00 ptZero

// ShapeKeyPoints.cpp: the key points and the curves between them
void	NORMD(long* angle);										// ROM 0x0021566c NORMD__FPl - into (-180, 180] degrees
long	Delta(long a, long b);									// ROM 0x00213988 Delta__FlT1 - how far apart two angles are
Boolean	SameAngle(FPoint a, FPoint b, long slop);				// ROM 0x002142e4 SameAngle__F6FPointT1l
void	ScaleToSize(FPoint* v, long size);						// ROM 0x002140ac ScaleToSize__FP6FPointl
FPoint	Project(FPoint a, FPoint b);							// ROM 0x00214108 Project__F6FPointT1
void	Reflect(FPoint* v, FPoint* axis);						// ROM 0x002147dc Reflect__FP6FPointT1
void	IntersectLine(FPoint* pt, FPoint* a1, FPoint* a2, FPoint* b1, FPoint* b2);	// ROM 0x002156dc IntersectLine__FP6FPointN41
void	InitGeneralPt(TDArray* shape, ULong index, FPoint pt);	// ROM 0x00212ed4 InitGeneralPt__FP7TDArrayUl6FPoint
Boolean	SetGeneralPt(TDArray* shape, ULong index, FPoint pt, UByte control, UByte flag9, UByte flag10);	// ROM 0x00212f2c SetGeneralPt__FP7TDArrayUl6FPointUcN24
Boolean	PlaceAfter(uint32_t* breaks, uint32_t after, uint32_t index);	// ROM 0x002134a8 PlaceAfter__FPUlUlT2
void	RLineOut2(FPoint* pts, char* marks, uint32_t* breaks, long depth, ULong first, ULong last);	// ROM 0x00213064 RLineOut2__FP6FPointPcPUllUlT5
long	RSmallDists(FPoint* pts, ULong last, Run* runs);		// ROM 0x00213314 RSmallDists__FP6FPointUlP3Run
Boolean	RLineOut(FPoint* pts, char* marks, uint32_t* breaks, Run* runs, long depth, ULong first, ULong last);	// ROM 0x00213444 RLineOut__FP6FPointPcPUlP3RunlUlT6
void	DeleteStrokes(ULong index, char count, uint32_t* n, FPoint* keys, char* kinds, uint32_t* breaks);	// ROM 0x00214d7c DeleteStrokes__FUlcPUlP6FPointPcT3
void	Collapser(uint32_t* n, FPoint* keys, char* kinds, uint32_t* breaks);	// ROM 0x00214b88 Collapser__FPUlP6FPointPcT1
Boolean	CheckSmooth(FPoint before, FPoint after, FPoint chordBefore, FPoint chordAfter);	// ROM 0x0021418c CheckSmooth__F6FPointN31
void	TVStrHead(ULong i, FPoint* keys, SplineSeg* segs);		// ROM 0x00213f4c TVStrHead__FUlP6FPointP9SplineSeg
void	TVStrTail(ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks, SplineSeg* segs, char* flags, long smalls);	// ROM 0x00213f78 TVStrTail__FUlP6FPointT2PcPUlP9SplineSegT4l
void	TVSplEnds(UByte atEnd, ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks, SplineSeg* segs, char* flags);	// ROM 0x00213df0 TVSplEnds__FUcUlP6FPointT3PcPUlP9SplineSegT5
void	TVSplStr(ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks, SplineSeg* segs, char* flags, long smalls);	// ROM 0x00213c68 TVSplStr__FUlP6FPointT2PcPUlP9SplineSegT4l
void	TVSplSpl(ULong i, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks, SplineSeg* segs, char* flags, long smalls);	// ROM 0x0021399c TVSplSpl__FUlP6FPointT2PcPUlP9SplineSegT4l
void	FindCubic1(uint32_t* n, FPoint* pts, FPoint* keys, char* kinds, uint32_t* breaks, Run* runs, SplineSeg* segs, char* flags);	// ROM 0x0021358c FindCubic1__FPUlP6FPointT2PcT1P3RunP9SplineSegT4
void	Connect(ULong n, long mode, long connections, SplineSeg* segs, char* segFlags, char* kinds, uint32_t* count, SplineSeg* shape, char* shapeFlags, char* shapeKinds);	// ROM 0x00214dec Connect__FUllT2P9SplineSegPcT5PUlT4N25
FPoint	DoConic(SplineSeg* seg);								// ROM 0x00214834 DoConic__FP9SplineSeg
FPoint	DoConicInfl(UByte first, SplineSeg* seg, SplineSeg* split);	// ROM 0x002149c8 DoConicInfl__FUcP9SplineSegT2
long	FindInflection(UByte cut, SplineSeg* segs, ULong i, char* flags, char* kinds, SplineSeg* split);	// ROM 0x00214344 FindInflection__FUcP9SplineSegUlPcT4T2
Boolean	MeetEnds(ULong n, ULong last, SplineSeg* segs, TDArray* shape);	// ROM 0x0021534c MeetEnds__FUlT1P9SplineSegP7TDArray

// One equation of the system (12 bytes): its coefficients in a handle.
struct Equation
{
	long		f00;			// +0x00
	long		f04;			// +0x04
	Handle		fCoeffs;		// +0x08
};

// The equations a shape's symmetries are written as (the ROM's EqSystem,
// 0x1f4 bytes on Classify's stack).
struct EqSystem
{
	long		f00;			// +0x00
	long		fCount;			// +0x04  how many equations there are
	Equation	fEqs[41];		// +0x08
};

#endif	/* __SHAPEGEOMETRY_H */
