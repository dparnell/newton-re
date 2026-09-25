/*
	File:		recognition/ShapeGeometry.cpp

	Contains:	The fitting and tidying the shape domain's Classify is
				made of.  See ShapeGeometry.h.

				FindKeyPoints and the fitting under it are ShapeKeyPoints.cpp.

				NOT YET RECONSTRUCTED: FindEllipses, FindEquations and the
				angle clustering (TTrend), SolveEquations and the minimiser,
				PlugNewVals, GlobalTrends and SnapPtToLC.  They answer
				"nothing found" and do nothing, so a shape comes out as its
				key points and curves, untidied.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ShapeGeometry.h"
#include "NewtonMemory.h"


// ROM 0x002257b8 ReleaseEqs__FP8EqSystem
// The equations' coefficients given back, and the system emptied.
void
ReleaseEqs(EqSystem* system)
{
	for (long i = 0; i < system->fCount; i++)
	{
		if (system->fEqs[i].fCoeffs != nil)
		{
			DeleteHandle(system->fEqs[i].fCoeffs);
			system->fEqs[i].fCoeffs = nil;
		}
	}
	system->fCount = 0;
}


// ROM 0x00215904 FindEllipses__FP17TGeneralShapeUnitP6GSTypePUlPl
// NOT YET RECONSTRUCTED.
Boolean
FindEllipses(TGeneralShapeUnit* /*unit*/, long* /*type*/, ULong* /*score*/, long* /*angle*/)
{
	return false;
}


// ROM 0x002231e0 FindEquations__FP17TGeneralShapeUnitPlP8EqSystemP6GSTypePUlT2
// NOT YET RECONSTRUCTED.
Boolean
FindEquations(TGeneralShapeUnit* /*unit*/, long* /*values*/, EqSystem* /*system*/,
			  long* /*type*/, ULong* /*score*/, long* /*angle*/)
{
	return false;
}


// ROM 0x0020fae8 SolveEquations__FP8EqSystemPl
// NOT YET RECONSTRUCTED.
Boolean
SolveEquations(EqSystem* /*system*/, long* /*values*/)
{
	return false;
}


// ROM 0x00225560 PlugNewVals__FP17TGeneralShapeUnitPlP8EqSystem
// NOT YET RECONSTRUCTED.
void
PlugNewVals(TGeneralShapeUnit* /*unit*/, long* /*values*/, EqSystem* /*system*/)
{ }


// ROM 0x00211684 GlobalTrends__FP17TGeneralShapeUnitPl
// NOT YET RECONSTRUCTED.
void
GlobalTrends(TGeneralShapeUnit* /*unit*/, long* /*snapped*/)
{ }


// ROM 0x00211d00 SnapPtToLC__FP17TGeneralShapeUnit
// NOT YET RECONSTRUCTED.
void
SnapPtToLC(TGeneralShapeUnit* /*unit*/)
{ }
