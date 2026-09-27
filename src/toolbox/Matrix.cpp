/*
	File:		toolbox/Matrix.cpp

	Contains:	3x3 transformation matrices in 16.16 fixed point
				(Matrix.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Matrix.h"
#include "FixedMath.h"
#include "FixedMathExtra.h"
#include "Angles.h"

// ROM 0x0c101538 idMatrix
const Fixed	idMatrix[9] = { 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000 };


// ROM 0x00125114 SetIdentityMatrix
long
SetIdentityMatrix(Fixed* m)
{
	MxInit(m);
	return 0;
}


// ROM 0x0012512c RotateMatrix
// Turned by the degrees about (cx, cy): moved there, turned, moved back.
long
RotateMatrix(Fixed* m, Fixed degrees, Fixed cx, Fixed cy)
{
	MxMove(m, -cx, -cy);
	MxRotate(m, -DegToRad(degrees));
	MxMove(m, cx, cy);
	return 0;
}


// ROM 0x001255d0 TransformPoints
void
TransformPoints(Fixed* m, ULong count, Fixed* points)
{
	for (ULong i = 0; i < count; i++, points += 2)
		MxTransform(m, points);
}


// ROM 0x00125614 Concatenate
// into = into x m (the sums wrap, as the ARM's adds do).
void
Concatenate(Fixed* m, Fixed* into)
{
	Fixed product[9];
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
		{
			Fixed sum = 0;
			for (int k = 0; k < 3; k++)
				sum = WrapAdd(FixedMultiply(into[r * 3 + k], m[k * 3 + c]), sum);
			product[r * 3 + c] = sum;
		}
	MxCopy(product, into);
}


// ROM 0x00125698 MxInit
void
MxInit(Fixed* m)
{
	for (int i = 0; i < 9; i++)
		m[i] = idMatrix[i];
}


// ROM 0x001256b8 MxCopy
void
MxCopy(const Fixed* from, Fixed* to)
{
	if (from == to)
		return;
	for (int i = 0; i < 9; i++)
		to[i] = from[i];
}


// ROM 0x001256dc MxRotate
void
MxRotate(Fixed* m, Fixed radians)
{
	Fixed r[9];
	MxCopy(idMatrix, r);
	Fract sine = FractSin(radians);
	r[0] = FractCos(radians) >> 14;
	r[1] = -(sine >> 14);
	r[3] = sine >> 14;
	r[4] = r[0];
	Concatenate(r, m);
}


// ROM 0x0012573c MxScale
void
MxScale(Fixed* m, Fixed sx, Fixed sy)
{
	Fixed s[9];
	MxCopy(idMatrix, s);
	s[0] = sx;
	s[4] = sy;
	Concatenate(s, m);
}


// ROM 0x00125780 MxTransform
// (x, y) = (x m0 + y m3 + m6, x m1 + y m4 + m7).
void
MxTransform(Fixed* m, Fixed* point)
{
	Fixed x = point[0];
	Fixed y = point[1];
	point[0] = WrapAdd(WrapAdd(FixedMultiply(m[0], x), FixedMultiply(m[3], y)), m[6]);
	point[1] = WrapAdd(WrapAdd(FixedMultiply(m[1], x), FixedMultiply(m[4], y)), m[7]);
}


// ROM 0x001257f8 MxMove
void
MxMove(Fixed* m, Fixed dx, Fixed dy)
{
	Fixed t[9];
	MxCopy(idMatrix, t);
	t[6] = dx;
	t[7] = dy;
	Concatenate(t, m);
}
