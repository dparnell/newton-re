/*
	File:		qd/RegionVars.cpp

	Contains:	The region holders: the one-entry region cache, TRegionVar,
				TRegion, TRegionStruct, TRectangularRegion.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "RegionVars.h"
#include "NewtonExceptions.h"
#include "objects.h"
#include "OSErrors.h"

// the cache: one region kept between uses
static RgnHandle gCachedRgn = nil;			// ROM 0x0c1018a0 gCachedRgn


// ROM 0x00199fb0 NewCachedRgn__Fv
// The cached region if there is one, else a new one; exOutOfMemory
// (kError_No_Memory) when no region can be made.
RgnHandle
NewCachedRgn(void)
{
	RgnHandle rgn = gCachedRgn;
	if (rgn != nil)
	{
		gCachedRgn = nil;
		return rgn;
	}
	rgn = NewRgn();
	if (rgn == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	return rgn;
}


// ROM 0x0019a030 DisposeCachedRgn__FPP6Region
// The region becomes the cached one when the cache is empty, else it is
// disposed.
void
DisposeCachedRgn(RgnHandle rgn)
{
	if (gCachedRgn == nil)
		gCachedRgn = rgn;
	else if (rgn != nil)
		DisposeRgn(rgn);
}


/*------------------------------------------------------------------------------
	T R e g i o n V a r
	A local that owns a region for its scope.  The ROM's object is 0x14
	bytes: the handle, then an exception cleanup record (kExceptionCleanup,
	the object, DisposeTRegionVar) so that a Throw through the scope gives
	the region back; the host registers the same record, since longjmp
	skips destructors.
------------------------------------------------------------------------------*/

// ROM 0x0019a054 DisposeTRegionVar__FPv
static void
DisposeTRegionVar(void* object)
{
	((TRegionVar*) object)->~TRegionVar();
}


// ROM 0x0019a05c __ct__10TRegionVarFv
TRegionVar::TRegionVar()
{
	fRegion = NewCachedRgn();
	fCleanup.header.catchType = kExceptionCleanup;
	fCleanup.object = this;
	fCleanup.function = DisposeTRegionVar;
	AddExceptionHandler((CatchHeader*) &fCleanup);
}


// ROM 0x0019a0b0 __ct__10TRegionVarFR7TRegion
TRegionVar::TRegionVar(TRegion& rgn)
{
	fRegion = rgn.StealRegion();
	fCleanup.header.catchType = kExceptionCleanup;
	fCleanup.object = this;
	fCleanup.function = DisposeTRegionVar;
	AddExceptionHandler((CatchHeader*) &fCleanup);
}


// ROM 0x0019a10c __dt__10TRegionVarFv
TRegionVar::~TRegionVar()
{
	RemoveExceptionHandler((CatchHeader*) &fCleanup);
	if (fRegion != nil)
		DisposeCachedRgn(fRegion);
	fRegion = nil;
}


// ROM 0x0019a148 __as__10TRegionVarFR7TRegion
TRegionVar&
TRegionVar::operator=(TRegion& rgn)
{
	DisposeCachedRgn(fRegion);
	fRegion = rgn.StealRegion();
	return *this;
}


// ROM 0x0019a174 StealRegion__10TRegionVarFv
RgnHandle
TRegionVar::StealRegion()
{
	RgnHandle rgn = fRegion;
	fRegion = nil;
	return rgn;
}


/*------------------------------------------------------------------------------
	T R e g i o n
------------------------------------------------------------------------------*/

// ROM 0x0019a290 __ct__7TRegionFR10TRegionVar
TRegion::TRegion(TRegionVar& rgn)
{
	fRegion = rgn.StealRegion();
}


// (the copy a return value makes: the ROM's compiler passes the result
// object to the callee, so the ROM has no copy constructor)
TRegion::TRegion(const TRegion& rgn)
{
	fRegion = ((TRegion&) rgn).StealRegion();
}


// ROM 0x0019a2cc __dt__7TRegionFv
TRegion::~TRegion()
{
	if (fRegion != nil)
		DisposeCachedRgn(fRegion);
}


// ROM 0x0019a300 StealRegion__7TRegionFv
RgnHandle
TRegion::StealRegion()
{
	RgnHandle rgn = fRegion;
	fRegion = nil;
	return rgn;
}


/*------------------------------------------------------------------------------
	T R e g i o n S t r u c t
------------------------------------------------------------------------------*/

// ROM 0x0019a188 __ct__13TRegionStructFv
TRegionStruct::TRegionStruct()
{
	fRegion = NewCachedRgn();
}


// ROM 0x0019a1bc __dt__13TRegionStructFv
TRegionStruct::~TRegionStruct()
{
	DisposeCachedRgn(fRegion);
}


// ROM 0x0019a1ec __as__13TRegionStructFR7TRegion
TRegionStruct&
TRegionStruct::operator=(TRegion& rgn)
{
	DisposeCachedRgn(fRegion);
	fRegion = rgn.StealRegion();
	return *this;
}


/*------------------------------------------------------------------------------
	T R e c t a n g u l a r R e g i o n
------------------------------------------------------------------------------*/

// ROM 0x0019a314 __ct__18TRectangularRegionFRC5TRect
TRectangularRegion::TRectangularRegion(const Rect& r)
{
	fRegionPtr = &fData;
	fData.rgnSize = kRectRgnSize;
	fData.filler = 0;
	fData.rgnBBox = r;
}
