/*
	File:		recognition/ShapeGeometry.cpp

	Contains:	The fitting and tidying the shape domain's Classify is
				made of.  See ShapeGeometry.h.

				FindKeyPoints and the fitting under it are ShapeKeyPoints.cpp.

				FindEllipses is ShapeEllipses.cpp, SolveEquations ShapeSolver.cpp,
				FindEquations and PlugNewVals ShapeEquations.cpp, the
				clustering ShapeTrends.cpp, and SnapPtToLC and GlobalTrends
				ShapeSnapping.cpp; this file keeps ReleaseEqs.

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
