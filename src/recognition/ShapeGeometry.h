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

// A left shift as the ARM does it: a 32-bit word that wraps, where the
// host's shift of a negative or too-large long is undefined.
inline long	ShiftLeft(long v, int n)	{ return (long) (int32_t) ((uint32_t) v << n); }

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

// ShapeEllipses.cpp: the linear algebra the ellipse fit is solved with
void	Decomp(ULong n, long ndim, Fixed* a, long* pivots, Fixed* det);	// ROM 0x00125180 Decomp
void	Solve(ULong n, long ndim, Fixed* a, long* pivots, Fixed* b);	// ROM 0x0012542c Solve

// One equation of the system (the ROM's Linear, 12 bytes): a linear
// form sum c[i] x[i] over the variables with x[0] = 1, which the solver
// wants to be nought.  fCoeffs holds 37 Fixed coefficients.
struct Equation
{
	long		fN;				// +0x00  the highest variable it mentions
	UByte		fKind;			// +0x04  0: a linear equation, squared into the function
	Handle		fCoeffs;		// +0x08  'cof0'
};
typedef Equation Linear;

// The equations a shape's symmetries are written as (the ROM's EqSystem,
// 0x1f4 bytes on Classify's stack).
struct EqSystem
{
	long		fN;				// +0x00  how many variables there are
	long		fCount;			// +0x04  how many equations there are
	Equation	fEqs[42];		// +0x08  ROM BUG: 41 fit, but NewCoeffs makes a 42nd (see there)
};

// A quadratic form over [1, x1 .. xn] as an upper triangle: row i is a
// handle of n+1 Fixed coefficients, of which [i..n] are used.
struct Bilinear
{
	long		fN;				// +0x00
	Handle		fRows[37];		// +0x04
};

// One product term of a MixFunc: x[v1]*x[v2] + x[v3]*x[v4] when fKind is
// 1, minus otherwise.  Nothing in the ROM ever makes one (fMixCount is only
// ever set to nought), so the minimiser only ever sees the quadratic.
struct MixTerm
{
	long		fKind;			// +0x00
	long		fV[4];			// +0x04
};

// What the minimiser minimises (the ROM's MixFunc, the global
// currFunction): the quadratic plus the sum of the absolute values of the
// mix terms.
struct MixFunc
{
	Bilinear	fQuad;			// +0x00
	long		fMixCount;		// +0x98
	MixTerm		fMix[5];		// +0x9c
};

// Row i of the gradient (the ROM's MixGradEl, 0x20 bytes): the linear
// form the quadratic's derivative by x[i] is, and for each mix term the
// variable its derivative is (negative: minus that variable; 0: none).
struct MixGradEl
{
	long		fN;				// +0x00
	UByte		fFlag;			// +0x04
	Handle		fCoeffs;		// +0x08  'cof1'
	long		fMix[5];		// +0x0c
};

// ShapeSolver.cpp: the system solved by minimising the sum of the squares
extern MixFunc		currFunction;						// ROM 0x0c106f10 currFunction
extern MixGradEl*	currGradient;						// ROM 0x0c104c90 currGradient

Boolean	SolveEquations(EqSystem* system, long* values);			// ROM 0x0020fae8 SolveEquations__FP8EqSystemPl
void	CalcSolutionBounds(const long* x, long n, FRect* bounds);	// ROM 0x0020fcfc CalcSolutionBounds__FPCllP5FRect
Boolean	InitFunction(long n, MixFunc* function, Bilinear* scratch);	// ROM 0x0020fd94 InitFunction__FlP7MixFuncP8Bilinear
long	TheFunction(long n, long* x, long* terms);				// ROM 0x0020fee4 TheFunction__FlPlT2
void	TheGradient(long n, long* x, long* terms, long* gradient);	// ROM 0x00210028 TheGradient__FlPlN22
void	MapSolutionToBounds(long* x, long n, const FRect& bounds);	// ROM 0x00210150 MapSolutionToBounds__FPllRC5FRect
void	SquareLinear(Linear* linear, Bilinear* square);			// ROM 0x002101e8 SquareLinear__FP6LinearP8Bilinear
void	AddBilinears(Bilinear* a, Bilinear* b, Bilinear* sum);	// ROM 0x002102cc AddBilinears__FP8BilinearN21
void	InitGradient(MixGradEl* gradient);						// ROM 0x0021034c InitGradient__FP9MixGradEl
Boolean	FindGradient(MixFunc* function, MixGradEl* gradient);	// ROM 0x00210370 FindGradient__FP7MixFuncP9MixGradEl
void	ReleaseGradient(MixGradEl* gradient);					// ROM 0x0021052c ReleaseGradient__FP9MixGradEl
void	ReleaseBilin(Bilinear* bilinear);						// ROM 0x0021056c ReleaseBilin__FP8Bilinear

// the minimiser: Numerical Recipes' conjugate gradients in 16.16
typedef long	(*NFunction)(long n, long* x, long* terms);
typedef void	(*NGradient)(long n, long* x, long* terms, long* gradient);
typedef long	(*Function1D)(long t, long n, long* p, long* dir, NFunction f, long* terms);
typedef long	(*Gradient1D)(long t, long n, long* p, long* dir, NGradient df, long* terms);

Boolean	Minimize(long* p, long n, long ftol, long* iterations, long* fret, NFunction f, NGradient df);	// ROM 0x00219128 Minimize__FPllT2N21PFlPlT2_lPFlPlN22_v
void	LineMinimize(long n, long* p, long* dir, long* fret, long* terms, NFunction f, NGradient df);	// ROM 0x002193a0 LineMinimize__FlPlN32PFlPlT2_lPFlPlN22_v
long	Func1D(long t, long n, long* p, long* dir, NFunction f, long* terms);	// ROM 0x002194a0 Func1D__FlT1PlT3PFlPlT2_lT3
long	DFunc1D(long t, long n, long* p, long* dir, NGradient df, long* terms);	// ROM 0x00219510 DFunc1D__FlT1PlT3PFlPlN22_vT3
void	BracketMin(long* a, long* b, long* c, long* fa, long* fb, long* fc, Function1D f, long n, long* p, long* dir, NFunction nf);	// ROM 0x002195fc BracketMin__FPlN51PFlT1PlT3PFlPlT2_lT3_llN21PFlPlT2_l
long	Minimize1D(long ax, long bx, long cx, Function1D f, Gradient1D df, long n, long* p, long* dir, NFunction nf, NGradient ndf, long tol, long* xmin, long* terms);	// ROM 0x002199d8 Minimize1D__FlN21PFlT1PlT3PFlPlT2_lT3_lPFlT1PlT3PFlPlN22_vT3_lT1PlT7PFlPlT2_lPFlPlN22_vT1N27

// ShapeTrends.cpp: the clustering of lengths and angles.
// One cluster of a trend (0x1c bytes).
struct Cluster
{
	long		fMean;			// +0x00
	Fixed		fVar;			// +0x04  the spread about the mean
	long		fSum;			// +0x08
	long		fMin;			// +0x0c
	long		fMax;			// +0x10
	long		fCount;			// +0x14
	long		fValue;			// +0x18  the value it started with: what a value joining it is taken as
};

class TTrend : public TDArray
{
public:
	static TTrend*	Make(long tolerance);					// ROM 0x0022bbb8 Make__6TTrendSFl
	long			ITrend(long tolerance);					// ROM 0x0022bc28 ITrend__6TTrendFl
	virtual void	Dispose(void);							// ROM 0x0022be04 Dispose__6TTrendFv

	long			FindCluster(long value);				// ROM 0x0022bc78 FindCluster__6TTrendFl
	Boolean			NewCluster(long index, long value);		// ROM 0x0022bce8 NewCluster__6TTrendFlT1
	long			AddToCluster(long index, long value);	// ROM 0x0022be08 AddToCluster__6TTrendFlT1
	Boolean			AddToTrend(long value, long* found, UByte add);	// ROM 0x0022bfcc AddToTrend__6TTrendFlPlUc
	long			Attach(long index, long value);			// ROM 0x0022c248 Attach__6TTrendFlT1
	Boolean			Merge(long index, Cluster* a, Cluster* b);	// ROM 0x0022c3a4 Merge__6TTrendFlP7ClusterT2
	Boolean			MergeCheck(long index, long bias);		// ROM 0x0022c610 MergeCheck__6TTrendFl4Bias

	Cluster*		At(long index)		{ return (Cluster*) GetEntry(index); }

	long			fFirst;			// +0x20  the first cluster's mean
	long			fLast;			// +0x24  the last cluster's mean
	long			fValues;		// +0x28  how many values have been put in
	long			fSpreadWeight;	// +0x2c
	Fixed			fSlope;			// +0x30  (last - first) / (clusters - 1)
	Fixed			fSpread;		// +0x34  the clusters' average spread
	long			fTolerance;		// +0x38  the least distance that joins a cluster
};

Boolean	BeforeCluster(Cluster* cluster, long value);			// ROM 0x0022bdb0 BeforeCluster__FP7Clusterl
Boolean	InCluster(Cluster* cluster, long value);				// ROM 0x0022bdc8 InCluster__FP7Clusterl
Fixed	VarStretch(long count);									// ROM 0x0022bde0 VarStretch__Fl

#endif	/* __SHAPEGEOMETRY_H */
