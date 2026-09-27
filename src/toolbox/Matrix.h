/*
	File:		toolbox/Matrix.h

	Contains:	3x3 transformation matrices in 16.16 fixed point.

				A point (x, y) is transformed as the row [x y 1] times the
				matrix, so the translation is the third row (elements 6
				and 7); each operation is concatenated onto the matrix
				after what it already does (`Concatenate(m, into)` makes
				`into` = `into` x `m`).  The ink's strokes are turned and
				scaled through these (`TStroke::Rotate`/`Scale`,
				`recognition/Stroke.h`).

	Reconstructed from the MP2x00 US ROM (0x00125114-0x0012583c, but
	Decomp and Solve, which are recognition/ShapeGeometry.h's); each
	function cites its origin.
*/

#ifndef __MATRIX_H
#define __MATRIX_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

typedef Fixed	Matrix[9];

extern const Fixed	idMatrix[9];								// ROM 0x0c101538 idMatrix

long	SetIdentityMatrix(Fixed* m);							// ROM 0x00125114 SetIdentityMatrix - ==> 0
long	RotateMatrix(Fixed* m, Fixed degrees, Fixed cx, Fixed cy);	// ROM 0x0012512c RotateMatrix - turned about (cx, cy); ==> 0
void	TransformPoints(Fixed* m, ULong count, Fixed* points);	// ROM 0x001255d0 TransformPoints - (x, y) pairs
void	Concatenate(Fixed* m, Fixed* into);						// ROM 0x00125614 Concatenate
void	MxInit(Fixed* m);										// ROM 0x00125698 MxInit - the identity
void	MxCopy(const Fixed* from, Fixed* to);					// ROM 0x001256b8 MxCopy
void	MxRotate(Fixed* m, Fixed radians);						// ROM 0x001256dc MxRotate
void	MxScale(Fixed* m, Fixed sx, Fixed sy);					// ROM 0x0012573c MxScale
void	MxTransform(Fixed* m, Fixed* point);					// ROM 0x00125780 MxTransform
void	MxMove(Fixed* m, Fixed dx, Fixed dy);					// ROM 0x001257f8 MxMove

#endif	/* __MATRIX_H */
