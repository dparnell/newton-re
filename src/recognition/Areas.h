/*
	File:		recognition/Areas.h

	Contains:	TRecArea, a recognition area - what a view's recognition
				configuration (its viewFlags' recognition bits and its
				recConfig frame) becomes for the recogniser: the unit types
				it accepts with the recognisers that handle them (a
				TTypeAssoc), the domains to run and their parameters, three
				dictionary chains for the word recogniser, and the id of the
				view it stands for.  Areas are shared by use count
				(Clone/Release).  TAreaList is a list of the areas a unit
				lies in; its last area is the merged one.  The ROM's
				TRecArea is 0x30 bytes.

				NOT YET RECONSTRUCTED: the area cache (gAreaCache: the areas
				built for the views hit, InitAreas/GetAreasHit) and the
				dictionary chains.

	Reconstructed from the MP2x00 US ROM (0x0021c1ac-0x0021c7c4); each
	function cites its origin.
*/

#ifndef __AREAS_H
#define __AREAS_H

#include "RecObject.h"

class TDictChain;
class TDomain;


// One entry of a TTypeAssoc: a unit type, the domain that handles it, the
// parameter block it is handled with, the record the domain keeps about
// it, and when its units are arbitrated.
struct Assoc
{
	ULong		fType;			// +0x00  the unit type this entry is for
	TDomain*	fDomain;		// +0x04  the recogniser that handles it
	Handle		fParams;		// +0x08  its parameter block
	void*		fInfo;			// +0x0c  the domain's own record (a dInfoRec); part of what makes an entry unique
	ULong		fUnknown10;		// +0x10  and so is this
	ULong		fArbitrateTime;	// +0x14  how its units are arbitrated (kArbitrateExternally: not by the arbiter at all)
	Boolean		fSharedParams;	// +0x18  the parameters are someone else's: not freed with the entry
};

// The arbitrate times an association can carry.  Anything else is a
// number of ticks the arbiter waits.
enum
{
	kArbitrateAtOnce		= 1,	// as soon as the unit is ready (TRecArea::fArbitrateNow counts these)
	kArbitrateExternally	= 2		// not through the arbiter: handled the moment it is made
};


// The types a recognition area takes, or the domains it runs, each with
// the parameters to run it with.  A sorted array of Assoc records - sorted
// by type, so that merging two areas' associations keeps the order - which
// is what makes AddAssoc a search and an insert rather than an append.
class TTypeAssoc : public TDArray
{
public:
	static TTypeAssoc*	Make(void);			// ROM 0x0022c778 Make__10TTypeAssocSFv
	long			ITypeAssoc(void);			// ROM 0x0022c7e0 ITypeAssoc__10TTypeAssocFv - ==> 0, or an error

	virtual void		IDispose(void);			// ROM 0x0022c7ec IDispose__10TTypeAssocFv - the parameter blocks that are ours freed with it
	virtual void		Dump(TMsg* msg);			// ROM 0x0022ca20 Dump__10TTypeAssocFP4TMsg (nothing)

	TTypeAssoc*		Copy(void);				// ROM 0x0022c878 Copy__10TTypeAssocFv
	ULong			AddAssoc(const Assoc* assoc);	// ROM 0x0022c8d0 AddAssoc__10TTypeAssocFP5Assoc - ==> its index, -1 for no memory
	void			MergeAssoc(TTypeAssoc* other);	// ROM 0x0022c998 MergeAssoc__10TTypeAssocFP10TTypeAssoc
	Assoc*			GetAssoc(ULong index);		// ROM 0x0022ca18 GetAssoc__10TTypeAssocFUl
};

class TRecArea : public TRecObject
{
public:
	static TRecArea*	Make(ULong viewFlags, ULong flags);		// ROM 0x0021c1ac Make__8TRecAreaSFUlT1

	virtual void		Dispose(void);							// ROM 0x0021c260 Dispose__8TRecAreaFv (released; gone when no user is left)
	virtual void		Dump(TMsg* msg);						// ROM 0x0021c7c0 Dump__8TRecAreaFP4TMsg (nothing)
	virtual long		SizeInBytes(void);						// ROM 0x0021c704 SizeInBytes__8TRecAreaFv
	virtual void		IDispose(void);							// ROM 0x0021c38c IDispose__8TRecAreaFv

	Handle				GetInfoFor(ULong type, Boolean make);	// ROM 0x0021c288 GetInfoFor__8TRecAreaFUlUc - the parameter block the area runs a domain with
	void				ParamsAllSet(ULong type);				// ROM 0x0021c400 ParamsAllSet__8TRecAreaFUl - the domain told its parameters are complete

	void				Clone(void);							// ROM 0x0021c67c Clone__8TRecAreaFv
	Boolean				Release(void);							// ROM 0x0021c6e8 Release__8TRecAreaFv - ==> whether no user is left

	long				fUsers;			// +0x08  Clone/Release (0: one user)
	ULong				fViewFlags;		// +0x0c  the recognition bits of the view's viewFlags
	ULong				fUnused10;		// +0x10
	long				fArbitrateNow;	// +0x14  the types added with arbitrate time 1
	TTypeAssoc*			fTypes;			// +0x18  the unit types -> recognisers (NOT YET)
	TTypeAssoc*			fDomains;		// +0x1c  the domains to run, with their parameter blocks (NOT YET)
	TDictChain*			fDictionaries[3];	// +0x20  the word recogniser's chains (NOT YET)
	ULong				fViewId;		// +0x2c  the view's id (TView::fId)
};

class TAreaList : public TDArray
{
public:
	static TAreaList*	Make(void);								// ROM 0x0021c498 Make__9TAreaListSFv
	long				IAreaList(void);						// ROM 0x0021c500 IAreaList__9TAreaListFv

	virtual void		Dispose(void);							// ROM 0x0021c50c Dispose__9TAreaListFv (the areas released; the list gone when no user is left)
	virtual void		IDispose(void);							// ROM 0x0021c56c IDispose__9TAreaListFv

	void				Clone(void);							// ROM 0x0021c570 Clone__9TAreaListFv - the areas too
	TRecArea*			GetArea(ULong index);					// ROM 0x0021c5b8 GetArea__9TAreaListFUl
	long				AddArea(TRecArea* area);				// ROM 0x0021c5d8 AddArea__9TAreaListFP8TRecArea - cloned; ==> 0, or 1 for no memory
	Boolean				FindMatchingView(ULong viewId);			// ROM 0x0021c628 FindMatchingView__9TAreaListFUl
	TRecArea*			GetMergedArea(void);					// ROM 0x0021c6dc GetMergedArea__9TAreaListFv - the last one
};

// The areas built for the views the pen has been over, kept so that the
// next stroke in the same view does not have to build them again.
//
// NOT YET RECONSTRUCTED: everything that fills it (InitAreas,
// GetAreasHit); with nothing in it there is nothing to purge either.
extern TArray*	gAreaCache;								// ROM 0x0c1008a0 gAreaCache

void	PurgeAreaCache(void);							// ROM 0x0003485c PurgeAreaCache__Fv - every area in it let go, the array emptied and shrunk

#endif	/* __AREAS_H */
