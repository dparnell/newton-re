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
