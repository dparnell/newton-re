/*
	File:		qd/RegionVars.h

	Contains:	The C++ region holders the view system passes around.  A
				TBaseRegion is a region handle by value - what Draw, Update,
				Invalidate, ... take as their argument; the host passes a
				plain RgnHandle.  TRegionVar is a local that owns a region
				from the one-entry cache (NewCachedRgn) and gives it back
				when it goes out of scope; TRegion a region handed back from
				a function (a TRegionVar's region stolen, disposed by whoever
				receives it unless stolen again); TRegionStruct a member that
				owns its region; TRectangularRegion a rectangle as a region
				in place - a master pointer followed by the twelve-byte
				rectangular Region, so that the object's address is a
				RgnHandle (the ROM's 0x10-byte layout: never resized, only
				the source of a region operation).  The ROM keeps them with
				TRect's methods (0x00197564..0x00197d34: Overlaps, Union,
				...; the host's Rects.h has those as functions).

	Reconstructed from the MP2x00 US ROM (0x00197964-0x00197d14); each
	function cites its origin.
*/

#ifndef __REGIONVARS_H
#define __REGIONVARS_H

#ifndef __REGIONS_H
#include "Regions.h"
#endif
#ifndef __NEWTONEXCEPTIONS_H
#include "NewtonExceptions.h"
#endif

RgnHandle	NewCachedRgn(void);				// the cached region, or a new one (exOutOfMemory when none)
void		DisposeCachedRgn(RgnHandle rgn);	// into the cache when it is empty, else disposed

// a region handle held by an object
class TBaseRegion
{
public:
				TBaseRegion()					: fRegion(nil) { }
				TBaseRegion(RgnHandle rgn)		: fRegion(rgn) { }
	operator	RgnHandle() const				{ return fRegion; }

protected:
	RgnHandle	fRegion;			// +0x00
};

class TRegion;

// a local that owns a cached region for the scope
class TRegionVar : public TBaseRegion
{
public:
				TRegionVar();				// a new (empty) region from the cache
				TRegionVar(TRegion& rgn);	// the region stolen
				~TRegionVar();
	TRegionVar&	operator=(TRegion& rgn);
	RgnHandle	StealRegion();
	void		Take(RgnHandle rgn)				{ fRegion = rgn; }		// host: a region given to an emptied one

private:
				TRegionVar(const TRegionVar&);
	TRegionVar&	operator=(const TRegionVar&);

	ExceptionCleanup	fCleanup;	// +0x04  the region given back when a Throw unwinds the scope
};

// a region handed back by a function: whoever receives it disposes it
class TRegion : public TBaseRegion
{
public:
				TRegion(TRegionVar& rgn);		// the region stolen
				TRegion(const TRegion& rgn);	// stolen from the copied-from one (a return value)
				~TRegion();
	RgnHandle	StealRegion();
};

// a member that owns its region
class TRegionStruct : public TBaseRegion
{
public:
				TRegionStruct();
				~TRegionStruct();
	TRegionStruct&	operator=(TRegion& rgn);
	RgnHandle	Swap(RgnHandle rgn)				{ RgnHandle old = fRegion; fRegion = rgn; return old; }	// host: the handle exchanged, neither disposed
};

// a rectangle as a region in place: the object's address is the handle,
// fRegionPtr the master pointer to the region in fData
class TRectangularRegion
{
public:
				TRectangularRegion(const Rect& r);
	operator	RgnHandle()						{ return (RgnHandle) &fRegionPtr; }

private:
	Region*		fRegionPtr;			// +0x00  -> fData
	Region		fData;				// +0x04  {rgnSize 12, filler, rgnBBox}
};

#endif	/* __REGIONVARS_H */
