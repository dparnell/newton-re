/*
	File:		recognition/ShapeGeometry.cpp

	Contains:	The fitting and tidying the shape domain's Classify is
				made of.  See ShapeGeometry.h.

				FindKeyPoints and the fitting under it are ShapeKeyPoints.cpp.

				FindEllipses is ShapeEllipses.cpp, SolveEquations ShapeSolver.cpp,
				FindEquations and PlugNewVals ShapeEquations.cpp.

				NOT YET RECONSTRUCTED: GlobalTrends and SnapPtToLC, which
				do nothing, so a shape is not yet snapped onto the shapes
				already on the page.

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
